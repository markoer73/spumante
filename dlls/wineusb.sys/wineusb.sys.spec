# Wine USB Bus Driver Specification

@ stdcall DriverEntry(ptr ptr)
@ stdcall WineUsbSimulatePnpStart(ptr ptr)
@ stdcall WineUsbSimulateQueryRelations(ptr long ptr)
@ stdcall WineUsbSimulateQueryId(ptr long ptr long ptr)
@ stdcall WineUsbSimulateSubmitUrb(ptr ptr)
@ stdcall WineUsbSimulateSetupDiInstall(ptr)
@ stdcall WineUsbResetDriverState()
