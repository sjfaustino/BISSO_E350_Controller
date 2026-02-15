#ifndef LOGGING_MOCK_H
#define LOGGING_MOCK_H

#include "serial_logger.h"
#include "fault_logging.h"

// Mock state access (for tests to verify logging)
void mockLoggingReset();
int mockLoggingGetWarningCount();
int mockLoggingGetErrorCount();
int mockLoggingGetFaultCount();

#endif
