#pragma once
#include <QString>
#include <QDir>
#include <QByteArray>
#include <qt_windows.h>
#include <aclapi.h>

// Replace inherited access with an ACL for the current account and LocalSystem.
// A protected directory makes QSaveFile's temporary files private on creation.
inline bool makeWindowsPrivate(const QString &path, bool directory)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    QByteArray storage(bytes, '\0');
    const bool queried = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes);
    CloseHandle(token);
    if (!queried) return false;
    auto *user = reinterpret_cast<TOKEN_USER *>(storage.data());
    BYTE systemSid[SECURITY_MAX_SID_SIZE];
    DWORD sidBytes = sizeof(systemSid);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &sidBytes)) return false;
    EXPLICIT_ACCESSW entries[2]{};
    for (int i = 0; i < 2; ++i) {
        entries[i].grfAccessPermissions = FILE_ALL_ACCESS;
        entries[i].grfAccessMode = SET_ACCESS;
        entries[i].grfInheritance = directory ? SUB_CONTAINERS_AND_OBJECTS_INHERIT : NO_INHERITANCE;
        entries[i].Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entries[i].Trustee.TrusteeType = TRUSTEE_IS_USER;
    }
    entries[0].Trustee.ptstrName = reinterpret_cast<LPWSTR>(user->User.Sid);
    entries[1].Trustee.ptstrName = reinterpret_cast<LPWSTR>(systemSid);
    PACL acl = nullptr;
    if (SetEntriesInAclW(2, entries, nullptr, &acl) != ERROR_SUCCESS) return false;
    auto native = QDir::toNativeSeparators(path).toStdWString();
    const DWORD status = SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    LocalFree(acl);
    return status == ERROR_SUCCESS;
}
