#include "logging.h"

#include <library/logger/filter.h>
#include <library/logger/global/rty_formater.h>
#include <library/logger/log.h>


#include <util/system/mem_info.h>
#include <util/stream/printf.h>
#include <util/datetime/base.h>


namespace NMatrixnetLoggingImpl {
    TStringBuf StripFileName(TStringBuf string) {
        return string.RNextTok(LOCSLASH_C);
    }
}

class TCustomFuncLogger : public TLogBackend {
public:
    TCustomFuncLogger(TCustomLoggingFunction func)
        : LoggerFunc(func)
    {
    }
    void WriteData(const TLogRecord& rec) override {
        LoggerFunc(rec.Data, rec.Len);
    }
    void ReopenLog() override {
    }

private:
    TCustomLoggingFunction LoggerFunc = nullptr;
};

void SetCustomLoggingFunction(TCustomLoggingFunction lowPriorityFunc, TCustomLoggingFunction highPriorityFunc) {
    TCatBoostLogSettings::GetRef().Log.ResetBackend(new TCustomFuncLogger(lowPriorityFunc), new TCustomFuncLogger(highPriorityFunc));
}

void RestoreOriginalLogger() {
    TCatBoostLogSettings::GetRef().Log.RestoreDefaultBackend();
}


TCatboostLogEntry::TCatboostLogEntry(TCatboostLog* parent, const TSourceLocation& sourceLocation, TStringBuf customMessage, ELogPriority priority) : Parent(parent)
, SourceLocation(sourceLocation)
, CustomMessage(customMessage)
, Priority(priority)
{
    if (parent->NeedExtendedInfo()) {
        (*this) << CustomMessage << ": " << NLoggingImpl::GetLocalTimeS() << " " << NMatrixnetLoggingImpl::StripFileName(SourceLocation.File) << ":" << SourceLocation.Line << " ";
        RegularMessageStartOffset = this->Filled();
    }
}

void TCatboostLogEntry::DoFlush()
{
    if (IsNull()) {
        return;
    }
    Parent->Output(*this);
    Reset();
}


TCatboostLogEntry::~TCatboostLogEntry()
{
    try {
        Finish();
    }
    catch (...) {
    }
}

class TCatboostLog::TImpl : public TLog {
public:
    TImpl(TAutoPtr<TLogBackend> lowPriorityBackend, TAutoPtr<TLogBackend> highPriorityBackend)
        : LowPriorityLog(lowPriorityBackend)
        , HighPriorityLog(highPriorityBackend)
    {}
    void ResetBackend(THolder<TLogBackend>&& lowPriorityBackend, THolder<TLogBackend>&& highPriorityBackend) {
        LowPriorityLog.ResetBackend(std::move(lowPriorityBackend));
        HighPriorityLog.ResetBackend(std::move(highPriorityBackend));
    }
    void ResetTraceBackend(THolder<TLogBackend>&& traceBackend) {
        TraceLog.ResetBackend(std::move(traceBackend));
    }
    void WriteRegularLog(const TCatboostLogEntry& entry, bool outputExtendedInfo) {
        const size_t regularOffset = outputExtendedInfo ? 0 : entry.GetRegularMessageStartOffset();
        if (entry.Priority <= TLOG_WARNING) {
            HighPriorityLog.Write(entry.Data() + regularOffset, entry.Filled() - regularOffset);
        } else {
            LowPriorityLog.Write(entry.Data() + regularOffset, entry.Filled() - regularOffset);
        }
    }
    void WriteTraceLog(const TCatboostLogEntry& entry) {
        TraceLog.Write(entry.Data(), entry.Filled());
    }
private:
    TLog LowPriorityLog;
    TLog HighPriorityLog;
    TLog TraceLog;
};

TCatboostLog::TCatboostLog()
    : ImplHolder(new TCatboostLog::TImpl(CreateLogBackend("cout"), CreateLogBackend("cerr")))
{}

TCatboostLog::~TCatboostLog() {
}

void TCatboostLog::Output(const TCatboostLogEntry& entry) {
    if (entry.Filled() != 0) {
        if (LogPriority >= entry.Priority) {
            ImplHolder->WriteRegularLog(entry, OutputExtendedInfo);
        }
        if (HaveTraceLog) {
            ImplHolder->WriteTraceLog(entry);
        }
    }
}

void TCatboostLog::ResetBackend(THolder<TLogBackend>&& lowPriorityBackend, THolder<TLogBackend>&& highPriorityBackend) {
    ImplHolder->ResetBackend(std::move(lowPriorityBackend), std::move(highPriorityBackend));
}

void TCatboostLog::ResetTraceBackend(THolder<TLogBackend>&& traceBackend /*= THolder<TLogBackend>()*/) {
    HaveTraceLog = (bool)traceBackend;
    ImplHolder->ResetTraceBackend(std::move(traceBackend));
}

void TCatboostLog::RestoreDefaultBackend() {
    ImplHolder->ResetBackend(CreateLogBackend("cout"), CreateLogBackend("cerr"));
}

void ResetTraceBackend(const TString& name) {
    TCatBoostLogSettings::GetRef().Log.ResetTraceBackend(CreateLogBackend(name));
}
