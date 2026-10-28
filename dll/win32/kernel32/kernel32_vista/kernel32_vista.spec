
@ stdcall InitOnceBeginInitialize(ptr long ptr ptr)
@ stdcall InitOnceComplete(ptr long ptr)
@ stdcall InitOnceExecuteOnce(ptr ptr ptr ptr)
@ stdcall InitOnceInitialize(ptr) NTDLL.RtlRunOnceInitialize

@ stdcall GetFileInformationByHandleEx(long long ptr long)
@ stdcall -ret64 GetTickCount64()

@ stdcall InitializeSRWLock(ptr)
@ stdcall AcquireSRWLockExclusive(ptr)
@ stdcall AcquireSRWLockShared(ptr)
@ stdcall ReleaseSRWLockExclusive(ptr)
@ stdcall ReleaseSRWLockShared(ptr)
@ stdcall TryAcquireSRWLockExclusive(ptr) ntdll_vista.RtlTryAcquireSRWLockExclusive

@ stdcall InitializeConditionVariable(ptr)
@ stdcall SleepConditionVariableCS(ptr ptr long)
@ stdcall SleepConditionVariableSRW(ptr ptr long long)
@ stdcall WakeAllConditionVariable(ptr)
@ stdcall WakeConditionVariable(ptr)
@ stdcall WaitOnAddress(ptr ptr long long)
@ stdcall WakeByAddressAll(ptr)
@ stdcall WakeByAddressSingle(ptr)

@ stdcall InitializeCriticalSectionEx(ptr long long)

@ stdcall GetFirmwareEnvironmentVariableExA(str str ptr long long)
@ stdcall GetFirmwareEnvironmentVariableExW(wstr wstr ptr long long)
@ stdcall GetFirmwareType(ptr)
@ stdcall SetFirmwareEnvironmentVariableExA(str str ptr long long)
@ stdcall SetFirmwareEnvironmentVariableExW(str str ptr long long)

@ stdcall ApplicationRecoveryFinished(long)
@ stdcall ApplicationRecoveryInProgress(ptr)
@ stdcall CreateSymbolicLinkA(str str long)
@ stdcall CreateSymbolicLinkW(wstr wstr long)
@ stdcall EnumSystemLocalesEx(ptr long long ptr)
@ stdcall GetApplicationRecoveryCallback(ptr ptr ptr ptr ptr)
@ stdcall GetApplicationRestart(ptr wstr ptr ptr)
@ stdcall GetFileBandwidthReservation(ptr ptr ptr ptr ptr ptr)
@ stdcall GetFileMUIInfo(long wstr ptr ptr)
@ stdcall GetFileMUIPath(long wstr wstr ptr wstr ptr ptr)
@ stdcall GetFinalPathNameByHandleA(ptr str long long)
@ stdcall GetFinalPathNameByHandleW(ptr wstr long long)
@ stdcall GetLocaleInfoEx(wstr long ptr long)
@ stdcall GetSystemDefaultLocaleName(ptr long)
@ stdcall GetSystemPreferredUILanguages(long ptr wstr ptr)
@ stdcall GetThreadPreferredUILanguages(long ptr wstr ptr)
@ stdcall GetThreadUILanguage()
@ stdcall GetUILanguageInfo(long wstr wstr ptr ptr)
@ stdcall GetUserDefaultLocaleName(wstr long)
@ stdcall GetUserPreferredUILanguages(long ptr wstr ptr)
@ stdcall IsValidLocaleName(wstr)
@ stdcall LCIDToLocaleName(long wstr long long)
@ stdcall LocaleNameToLCID(wstr long)
@ stdcall OpenFileById(ptr ptr long long ptr long)
@ stdcall QueryFullProcessImageNameA(ptr long ptr ptr)
@ stdcall QueryFullProcessImageNameW(ptr long ptr ptr)
@ stdcall RegisterApplicationRecoveryCallback(ptr ptr long long)
@ stdcall RegisterApplicationRestart(wstr long)
@ stdcall SetFileBandwidthReservation(ptr long long long ptr ptr)
@ stdcall SetThreadPreferredUILanguages(long wstr ptr)
@ stdcall CompareStringOrdinal(ptr long ptr long long)

@ stdcall CloseThreadpool(ptr) ntdll_vista.TpReleasePool
@ stdcall CloseThreadpoolCleanupGroup(ptr) ntdll_vista.TpReleaseCleanupGroup
@ stdcall CloseThreadpoolCleanupGroupMembers(ptr long ptr) ntdll_vista.TpReleaseCleanupGroupMembers
@ stdcall CloseThreadpoolIo(ptr) ntdll_vista.TpReleaseIoCompletion
@ stdcall CloseThreadpoolTimer(ptr) ntdll_vista.TpReleaseTimer
@ stdcall CloseThreadpoolWait(ptr) ntdll_vista.TpReleaseWait
@ stdcall CloseThreadpoolWork(ptr) ntdll_vista.TpReleaseWork
@ stdcall CreateThreadpool(ptr)
@ stdcall CreateThreadpoolCleanupGroup()
@ stdcall CreateThreadpoolTimer(ptr ptr ptr)
@ stdcall CreateThreadpoolWait(ptr ptr ptr)
@ stdcall CreateThreadpoolWork(ptr ptr ptr)
@ stdcall FreeLibraryWhenCallbackReturns(ptr ptr) ntdll_vista.TpCallbackUnloadDllOnCompletion
@ stdcall GetNamedPipeClientProcessId(ptr ptr)
@ stdcall GetProductInfo(long long long long ptr) ntdll_vista.RtlGetProductInfo
@ stdcall GetSystemTimePreciseAsFileTime(ptr) kernel32.GetSystemTimeAsFileTime
@ stdcall GetThreadDescription(ptr ptr) # Win 10
@ stdcall SetThreadDescription(ptr wstr) # Win 10
@ stdcall SetThreadpoolThreadMaximum(ptr long) ntdll_vista.TpSetPoolMaxThreads
@ stdcall SetThreadpoolThreadMinimum(ptr long) ntdll_vista.TpSetPoolMinThreads
@ stdcall SetThreadpoolTimer(ptr ptr long long) ntdll_vista.TpSetTimer
@ stdcall SetThreadpoolWait(ptr long ptr) ntdll_vista.TpSetWait
@ stdcall SubmitThreadpoolWork(ptr) ntdll_vista.TpPostWork
@ stdcall TrySubmitThreadpoolCallback(ptr ptr ptr)
@ stdcall WaitForThreadpoolIoCallbacks(ptr long) ntdll_vista.TpWaitForIoCompletion
@ stdcall WaitForThreadpoolTimerCallbacks(ptr long) ntdll_vista.TpWaitForTimer
@ stdcall WaitForThreadpoolWaitCallbacks(ptr long) ntdll_vista.TpWaitForWait
@ stdcall WaitForThreadpoolWorkCallbacks(ptr long) ntdll_vista.TpWaitForWork
@ stdcall GetDynamicTimeZoneInformation(ptr)
@ stdcall GetTimeZoneInformationForYear(long ptr ptr)
@ stdcall SetDynamicTimeZoneInformation(ptr)
@ stdcall CancelIoEx(ptr ptr)
@ stdcall GetCurrentThreadStackLimits(ptr ptr)
@ stdcall GetProcessMitigationPolicy(ptr long ptr long)
@ stdcall SetProcessMitigationPolicy(long ptr long)
@ stdcall SetThreadInformation(long long ptr long)
@ stdcall WerRegisterRuntimeExceptionModule(wstr ptr)
@ stdcall PrefetchVirtualMemory(long long ptr long)
@ stdcall SetDefaultDllDirectories(long)
@ stdcall WerUnregisterRuntimeExceptionModule(wstr ptr)
@ stdcall GetMaximumProcessorGroupCount()
@ stdcall DiscardVirtualMemory(ptr long)
@ stdcall GetPackagePathByFullName(wstr ptr ptr)
@ stdcall GetPackagesByPackageFamily(wstr ptr ptr ptr ptr)
@ stdcall GetProcessInformation(long long ptr long)
@ stdcall SetProcessInformation(long long ptr long)
@ stdcall PowerCreateRequest(ptr)
@ stdcall PowerSetRequest(ptr long)
@ stdcall PowerClearRequest(ptr long)
@ stdcall CreateWaitableTimerExW(ptr wstr long long)
@ stdcall InitializeProcThreadAttributeList(ptr long long ptr)
@ stdcall UpdateProcThreadAttribute(ptr long ptr ptr ptr ptr ptr)
@ stdcall DeleteProcThreadAttributeList(ptr)
@ stdcall GetLogicalProcessorInformationEx(long ptr ptr)
@ stdcall GetNamedPipeServerProcessId(ptr ptr)
@ stdcall QueryProcessCycleTime(ptr ptr)
@ stdcall QueryThreadCycleTime(ptr ptr)
@ stdcall ResolveLocaleName(wstr ptr long)
@ stdcall SetFileInformationByHandle(ptr long ptr long)

@ stdcall -version=0x602+ AppPolicyGetProcessTerminationMethod(ptr ptr)
@ stdcall -version=0x602+ AppPolicyGetThreadInitializationType(ptr ptr)
@ stdcall -version=0x602+ AppPolicyGetShowDeveloperDiagnostic(ptr ptr)
@ stdcall -version=0x602+ AppPolicyGetWindowingModel(ptr ptr)
