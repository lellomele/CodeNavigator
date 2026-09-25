#include "crash.h"
#include <QDateTime>
#include <QDir>
#include <cstdio>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <dbghelp.h>
// clang-format on
static wchar_t crashLog[32768], crashDump[32768];
using DumpFunction = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                    PMINIDUMP_EXCEPTION_INFORMATION,
                                    PMINIDUMP_USER_STREAM_INFORMATION,
                                    PMINIDUMP_CALLBACK_INFORMATION);
static DumpFunction writeDump = nullptr;
static LONG WINAPI crashFilter(EXCEPTION_POINTERS *info) {
    auto f = CreateFileW(crashLog, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        char line[512];
        auto n = std::snprintf(line, sizeof(line),
                               "{\"event\":\"native_crash\",\"version\":\"0.2.0\",\"pid\":%lu,"
                               "\"thread\":%lu,\"exception\":%lu,\"address\":\"%p\"}\n",
                               GetCurrentProcessId(), GetCurrentThreadId(),
                               info->ExceptionRecord->ExceptionCode,
                               info->ExceptionRecord->ExceptionAddress);
        DWORD written;
        WriteFile(f, line, DWORD(n), &written, nullptr);
        FlushFileBuffers(f);
        CloseHandle(f);
    }
    auto dump = CreateFileW(crashDump, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
        if (writeDump)
            writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal, &exception,
                      nullptr, nullptr);
        FlushFileBuffers(dump);
        CloseHandle(dump);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
void installCrashDiagnostics(const QString &directory) {
#ifdef _WIN32
    const auto stem = QDir(directory).filePath(QStringLiteral("crash-%1-%2")
                                                   .arg(QDateTime::currentMSecsSinceEpoch())
                                                   .arg(GetCurrentProcessId()));
    auto copy = [](wchar_t *target, const QString &s) {
        s.left(32766).toWCharArray(target);
        target[qMin(s.size(), qsizetype(32766))] = 0;
    };
    copy(crashLog, stem + QStringLiteral(".jsonl"));
    copy(crashDump, stem + QStringLiteral(".dmp"));
    auto library = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library)
        writeDump = reinterpret_cast<DumpFunction>(GetProcAddress(library, "MiniDumpWriteDump"));
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(crashFilter);
#else
    Q_UNUSED(directory);
#endif
}
