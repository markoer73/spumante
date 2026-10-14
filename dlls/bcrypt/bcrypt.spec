# Wine bcrypt.dll export specification
# SPDX-License-Identifier: LGPL-2.1-or-later

@ stdcall BCryptOpenAlgorithmProvider(ptr wstr wstr long)
@ stdcall BCryptCloseAlgorithmProvider(ptr long)
@ stdcall BCryptGetProperty(ptr wstr ptr long ptr long)
@ stdcall BCryptGenerateSymmetricKey(ptr ptr ptr long ptr long long)
@ stdcall BCryptEncrypt(ptr ptr long ptr ptr long ptr long ptr long)
@ stdcall BCryptDecrypt(ptr ptr long ptr ptr long ptr long ptr long)
@ stdcall BCryptExportKey(ptr ptr wstr ptr long ptr long)
@ stdcall BCryptDestroyKey(ptr)
@ stub BCryptCreateMultiHash
@ stub BCryptProcessMultiOperations
@ stub BCryptDeriveKeyCapi
