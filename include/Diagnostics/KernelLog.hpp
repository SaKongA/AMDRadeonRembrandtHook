#pragma once

#include <stdarg.h>

class KernelLog {
public:
    enum class Category { Core, Hook, Hardware };

    static void logInfo(Category category, const char *component, const char *operation, const char *format, ...)
        __attribute__((format(printf, 4, 5)));
    static void logWarning(Category category, const char *component, const char *operation, const char *format, ...)
        __attribute__((format(printf, 4, 5)));
    static void logError(Category category, const char *component, const char *operation, const char *format, ...)
        __attribute__((format(printf, 4, 5)));
    static void logDiagnosticInfo(Category category, const char *component, const char *operation, const char *format, ...)
        __attribute__((format(printf, 4, 5)));
    static void logDiagnosticWarning(Category category, const char *component, const char *operation, const char *format, ...)
        __attribute__((format(printf, 4, 5)));
    static bool startNvramLogging();
    static void startDiskLogging();

private:
    static void writeLog(bool persist, Category category, const char *level, const char *component, const char *operation,
                         const char *format, va_list arguments);
};
