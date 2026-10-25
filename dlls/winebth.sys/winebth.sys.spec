# Wine Bluetooth Bus Driver spec file

@ stdcall DriverEntry(ptr ptr)
@ stdcall WineBthSimulatePnpStart(ptr ptr)
@ stdcall WineBthSimulateQueryRelations(ptr long ptr)
@ stdcall WineBthSimulateQueryId(ptr long ptr long ptr)
@ stdcall WineBthSimulateIoctl(ptr long ptr long ptr long ptr)
@ stdcall WineBthSimulateSetupDiInstall(ptr)
@ stdcall WineBthResetDriverState()
