# Wine bcrypt.dll export specification
# SPDX-License-Identifier: LGPL-2.1-or-later

@ stdcall BCryptOpenAlgorithmProvider(ptr wstr wstr long)
@ stdcall BCryptCloseAlgorithmProvider(ptr long)
@ stdcall BCryptGetProperty(ptr wstr ptr long ptr long)
@ stdcall BCryptSetProperty(ptr wstr ptr long long)
@ stdcall BCryptGenerateSymmetricKey(ptr ptr ptr long ptr long long)
@ stdcall BCryptImportKey(ptr ptr wstr ptr ptr long ptr long long)
@ stdcall BCryptImportKeyPair(ptr ptr wstr ptr ptr long long)
@ stdcall BCryptEncrypt(ptr ptr long ptr ptr long ptr long ptr long)
@ stdcall BCryptDecrypt(ptr ptr long ptr ptr long ptr long ptr long)
@ stdcall BCryptExportKey(ptr ptr wstr ptr long ptr long)
@ stdcall BCryptDuplicateKey(ptr ptr ptr long long)
@ stdcall BCryptDestroyKey(ptr)
@ stdcall BCryptGenRandom(ptr ptr long long)
@ stdcall BCryptCreateHash(ptr ptr ptr long ptr long long)
@ stdcall BCryptHashData(ptr ptr long long)
@ stdcall BCryptFinishHash(ptr ptr long long)
@ stdcall BCryptDuplicateHash(ptr ptr ptr long long)
@ stdcall BCryptDestroyHash(ptr)
@ stdcall BCryptHash(ptr ptr long ptr long ptr long)
@ stdcall BCryptDeriveKeyPBKDF2(ptr ptr long ptr long int64 ptr long long)
@ stdcall BCryptSignHash(ptr ptr ptr long ptr long ptr long)
@ stdcall BCryptVerifySignature(ptr ptr ptr long ptr long long)
@ stdcall BCryptFreeBuffer(ptr)
@ stdcall BCryptEnumProviders(wstr ptr ptr long)
@ stdcall BCryptResolveProviders(wstr long wstr wstr long long ptr ptr)
@ stdcall GetCipherInterface(wstr ptr long)
@ stdcall GetHashInterface(wstr ptr long)
@ stdcall GetRngInterface(wstr ptr long)
@ stub BCryptCreateMultiHash
@ stub BCryptProcessMultiOperations
@ stub BCryptDeriveKeyCapi
