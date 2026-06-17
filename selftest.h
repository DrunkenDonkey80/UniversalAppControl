#pragma once
int RunSelfTests(void);       // returns number of failed checks
int RunMonitorDebug(void);    // --monitor-debug: step-by-step DDC/CI probe
int RunVcpSweep(void);        // --vcp-sweep: probe all 256 VCP codes via real app context
