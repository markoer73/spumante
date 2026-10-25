# Task Left Summary

Executive summary of all remaining open tasks in `docs/TASK_QUEUE.md`.

> **CRITICAL RULE:** Completed tasks MUST be removed from this file. This file must strictly and exclusively list remaining uncompleted work.

---

## Remaining Open Tasks

- [ ] Fix `ole32`/`rpcrt4`/`services.exe` `RpcSs` startup synchronization, `IID_IServiceProvider` (`{6d5140c1-7436-11ce-8034-00aa006009fa}`) proxy/stub interface marshaling (`StdMarshalImpl_MarshalInterface` / `CoMarshalInterface` / `apartment_get_local_server_stream` `hr 0x80004002`), and `scmdatabase_autostart_services` shutdown ordering (`MountMgr` timeout `1053` and `PlugPlay`/`Eventlog`/`nsiproxy`/`NDIS`/`winebus`/`wineusb`/`winebth` shutdown race `1115`) during `wineboot --init`
- [ ] Fix `ntdll` activation context WinSxS manifest resolution (`parse_depend_manifests`) and `wine-install-*` manifest packaging so dependent assembly `Microsoft.Windows.Common-Controls` (`6.0.0.0`) resolves cleanly during `wineboot --init`
- [ ] Borrow and implement `SP_COPY_NOOVERWRITE` (`0x10`) file copy style handling in `dlls/setupapi` (`do_file_copyW`) to eliminate `err:setupapi:do_file_copyW Unsupported style(s) 0x10` during prefix initialization
- [ ] Borrow and fix `dlls/win32u` cross-thread/cross-process `WM_ERASEBKGND` (`msg 14`) HDC message packing in `pack_message` and `NtUserDrawIconEx` frame 0 retrieval to eliminate `fixme:msg:pack_message msg 14 (WM_ERASEBKGND) not supported yet` and `fixme:icon:NtUserDrawIconEx Error retrieving icon frame 0`
