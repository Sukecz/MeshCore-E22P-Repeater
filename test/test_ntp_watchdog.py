#!/usr/bin/env python3
"""Compile the production NTP methods with deterministic clock/UDP substitutes."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'examples/simple_repeater/MyMesh.cpp')
    source = parser.parse_args().source.read_text()
    methods = []
    for name in ('startTimeWatchdogSync', 'finishTimeWatchdogSync', 'runTimeWatchdog'):
        start = source.index('void MyMesh::' + name + '(')
        end = source.index('\n}\n', start) + 3
        methods.append(source[start:end])
    defines = '\n'.join(re.findall(r'^#define WIFI_NTP_\w+\s+\d+(?:UL)?$', source, re.M))
    harness = r'''
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
constexpr int WL_CONNECTED = 3;
struct { int connected = WL_CONNECTED; int status() { return connected; } } WiFi;
struct IPAddress {};
IPAddress timeWatchdogNtpServerIP() { return {}; }
constexpr int WIFI_NTP_SERVER_PORT = 123;
enum { TIME_WD_WAITING, TIME_WD_PENDING, TIME_WD_SYNCED, TIME_WD_NO_WIFI,
       TIME_WD_TIMEOUT, TIME_WD_BACKWARDS, TIME_WD_BAD_REPLY, TIME_WD_SEND_FAIL };
struct UDP {
  bool ready = false, send_ok = true;
  int sends = 0;
  uint8_t packet[48]{};
  bool begin(int) { return true; }
  bool beginPacket(IPAddress, int) { return send_ok; }
  void write(uint8_t*, unsigned) {}
  bool endPacket() { ++sends; return send_ok; }
  int parsePacket() { return ready ? 48 : 0; }
  void read(uint8_t* out, unsigned size) { memcpy(out, packet, size); ready = false; }
  void reply(uint32_t epoch) {
    uint32_t seconds = epoch + WIFI_NTP_UNIX_OFFSET;
    for (int i = 0; i < 4; ++i) packet[40+i] = seconds >> (24-8*i);
    ready = true;
  }
} ntp_udp;
bool ntp_udp_started = false;
struct Millis { uint32_t now = 1000; uint32_t getMillis() { return now; } } timer;
struct Clock {
  uint32_t epoch = 1800000000; int writes = 0;
  uint32_t getCurrentTime() { return epoch; }
  void setCurrentTime(uint32_t value) { epoch = value; ++writes; }
};
struct MyMesh {
  Millis* _ms = &timer;
  Clock clock;
  uint32_t time_watchdog_at = 0, time_watchdog_sent_at = 0;
  uint32_t time_watchdog_syncs = 0, time_watchdog_fails = 0, time_watchdog_last_ntp = 0;
  int32_t time_watchdog_last_delta = 0;
  uint8_t time_watchdog_state = TIME_WD_WAITING;
  bool time_watchdog_pending = false;
  Clock* getRTCClock() { return &clock; }
  uint32_t futureMillis(uint32_t delay) { return _ms->getMillis() + delay; }
  bool millisHasNowPassed(uint32_t deadline) { return int32_t(_ms->getMillis() - deadline) > 0; }
  void startTimeWatchdogSync();
  void finishTimeWatchdogSync(bool, uint32_t);
  void runTimeWatchdog();
};
'''
    cases = r'''
void check(bool ok, const char* name) {
  if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); }
  printf("PASS: %s\n", name);
}
void reset(uint32_t now = 1000) {
  timer.now = now; ntp_udp = UDP{}; ntp_udp_started = false; WiFi.connected = WL_CONNECTED;
}
int main() {
  reset(); MyMesh early; early.startTimeWatchdogSync();
  timer.now += 20; ntp_udp.reply(1800000010); early.runTimeWatchdog();
  check(!early.time_watchdog_pending && early.clock.writes == 1 && early.time_watchdog_syncs == 1,
        "reply at 20 ms applies before timeout");
  check(early.time_watchdog_at == timer.now + WIFI_NTP_WATCHDOG_OK_INTERVAL_MS,
        "successful reply retains six-hour interval");
  timer.now += 100; early.runTimeWatchdog();
  check(ntp_udp.sends == 1, "normal loops do not send extra requests");

  reset(); MyMesh timeout; timeout.startTimeWatchdogSync();
  timer.now += 2499; timeout.runTimeWatchdog();
  check(timeout.time_watchdog_pending && timeout.time_watchdog_fails == 0,
        "missing reply remains pending before deadline");
  timer.now += 2; timeout.runTimeWatchdog();
  check(!timeout.time_watchdog_pending && timeout.time_watchdog_state == TIME_WD_TIMEOUT &&
        timeout.time_watchdog_fails == 1 && timeout.time_watchdog_at == timer.now + WIFI_NTP_WATCHDOG_RETRY_MS,
        "timeout at 2501 ms retains ten-minute retry");
  timeout.runTimeWatchdog();
  check(ntp_udp.sends == 1, "timeout does not immediately resend");

  for (uint32_t epoch : {1800000000u, 1799999999u}) {
    reset(); MyMesh backwards; backwards.startTimeWatchdogSync();
    timer.now += 20; ntp_udp.reply(epoch); backwards.runTimeWatchdog();
    check(backwards.time_watchdog_state == TIME_WD_BACKWARDS && backwards.clock.writes == 0,
          "equal or older reply cannot set clock backwards");
  }
  reset(); MyMesh invalid; invalid.startTimeWatchdogSync();
  timer.now += 20; ntp_udp.reply(1600000000); invalid.runTimeWatchdog();
  check(invalid.time_watchdog_state == TIME_WD_BAD_REPLY && invalid.clock.writes == 0 &&
        invalid.time_watchdog_at == timer.now + WIFI_NTP_WATCHDOG_RETRY_MS,
        "invalid epoch is rejected and retried later");

  reset(UINT32_MAX - 10); MyMesh wrap; wrap.startTimeWatchdogSync();
  timer.now += 20; ntp_udp.reply(1800000010); wrap.runTimeWatchdog();
  check(wrap.time_watchdog_state == TIME_WD_SYNCED, "early reply across millis wrap");
  reset(UINT32_MAX - 10); MyMesh wrap_timeout; wrap_timeout.startTimeWatchdogSync();
  timer.now += 2501; wrap_timeout.runTimeWatchdog();
  check(wrap_timeout.time_watchdog_state == TIME_WD_TIMEOUT, "timeout across millis wrap");

  reset(); MyMesh disabled; disabled.runTimeWatchdog();
  check(ntp_udp.sends == 0, "disabled scheduler sends nothing");
  disabled.time_watchdog_at = timer.now + 180000; disabled.runTimeWatchdog();
  check(ntp_udp.sends == 0, "boot delay sends nothing early");
  timer.now += 180001; disabled.runTimeWatchdog();
  check(ntp_udp.sends == 1 && disabled.time_watchdog_pending, "due scheduler starts one request");
  reset(); MyMesh offline; WiFi.connected = 0; offline.startTimeWatchdogSync();
  check(ntp_udp.sends == 0 && offline.time_watchdog_state == TIME_WD_NO_WIFI &&
        offline.time_watchdog_at == timer.now + WIFI_NTP_WATCHDOG_RETRY_MS,
        "offline state retries without sending");
  reset(); MyMesh send_fail; ntp_udp.send_ok = false; send_fail.startTimeWatchdogSync();
  check(!send_fail.time_watchdog_pending && send_fail.time_watchdog_state == TIME_WD_SEND_FAIL,
        "send failure leaves no pending request");
}
'''
    with tempfile.TemporaryDirectory(prefix='meshcore-ntp-test-') as directory:
        cpp = Path(directory) / 'test.cpp'
        binary = Path(directory) / 'test'
        cpp.write_text('#include <initializer_list>\n' + defines + '\n' + harness + '\n'.join(methods) + cases)
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
