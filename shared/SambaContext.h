/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_SAMBA_CONTEXT_H
#define SMBFS_SAMBA_CONTEXT_H

#include <Locker.h>
#include <String.h>
#include <SupportDefs.h>

#include <libsmbclient.h>
#include <private/shared/AutoLocker.h>
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <time.h>


namespace Smb {


extern BLocker gGlobalSambaLock;
typedef AutoLocker<BLocker> GlobalSambaLocker;


extern void get_authentication(const char* server, const char* share,
	char*, int,
	char* username, int usernameMaxLength,
	char* password, int passwordMaxLength);


class SambaContext {
public:
	SambaContext()
		:
		fContext(smbc_new_context())
	{
		smbc_init(get_authentication, 0);
			// TODO, only once

		smbc_init_context(fContext);
		smbc_setFunctionAuthData(fContext, get_authentication);

		//SetDebug(10);
	}

	~SambaContext()
	{
		smbc_free_context(fContext, 1);
	}

	int GetDebug()
	{
		return smbc_getDebug(fContext);
	}

	void SetDebug(int level)
	{
		smbc_setDebug(fContext, level);
	}

	status_t Stat(const BString& url, struct stat* destination)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionStat(fContext)(fContext,
			url.String(), destination));
	}

	status_t FileTruncate(SMBCFILE* file, off_t newSize)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionFtruncate(fContext)(fContext, file,
			newSize));
	}

	status_t UpdateTime(const BString& url,
		const struct timespec& modificationTime)
	{
		assert(gGlobalSambaLock.IsLocked());
		timeval modificationTimeVal;
		modificationTimeVal.tv_sec = modificationTime.tv_sec;
		modificationTimeVal.tv_usec = modificationTime.tv_nsec / 1000;
		return _GetStatus(smbc_getFunctionUtimes(fContext)(fContext,
			url.String(), &modificationTimeVal));
	}

	status_t Open(const BString& url, int flags, SMBCFILE** outFile)
	{
		assert(gGlobalSambaLock.IsLocked());
		*outFile = smbc_getFunctionOpen(fContext)(fContext, url.String(),
			flags, 0);
		return *outFile != NULL ? B_OK : errno;
	}

	status_t Close(SMBCFILE* file)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionClose(fContext)(fContext, file));
	}

	status_t Create(const BString& url, mode_t mode, SMBCFILE** outFile)
	{
		assert(gGlobalSambaLock.IsLocked());
		*outFile = smbc_getFunctionCreat(fContext)(fContext, url, mode);
		return *outFile != NULL ? B_OK : errno;
	}

	status_t Seek(SMBCFILE* file, off_t offset)
	{
		assert(gGlobalSambaLock.IsLocked());
		off_t result = smbc_getFunctionLseek(fContext)(fContext, file, offset,
			SEEK_SET);
		return result != -1 ? B_OK : errno;
	}

	status_t Read(SMBCFILE* file, void* buffer, size_t* count)
	{
		assert(gGlobalSambaLock.IsLocked());
		ssize_t bytesRead = smbc_getFunctionRead(fContext)(fContext, file,
			buffer, *count);
		if (bytesRead < 0)
			return errno;
		*count = bytesRead;
		return B_OK;
	}

	status_t Write(SMBCFILE* file, const void* buffer, size_t* count)
	{
		assert(gGlobalSambaLock.IsLocked());
		ssize_t bytesWritten = smbc_getFunctionWrite(fContext)(fContext, file,
			buffer, *count);
		if (bytesWritten < 0)
			return errno;
		*count = bytesWritten;
		return B_OK;
	}

	status_t Unlink(const BString& url)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionUnlink(fContext)(fContext,
			url.String()));
	}

	status_t Rename(const BString& fromURL, const BString& toURL)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionRename(fContext)(fContext,
			fromURL.String(), fContext, toURL.String()));
	}

	status_t CreateDir(const BString& url, mode_t mode)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionMkdir(fContext)(fContext,
			url.String(), mode));
	}

	status_t RemoveDir(const BString& url)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionRmdir(fContext)(fContext,
			url.String()));
	}

	status_t OpenDir(const BString& url, SMBCFILE** outDir)
	{
		assert(gGlobalSambaLock.IsLocked());
		*outDir = smbc_getFunctionOpendir(fContext)(fContext, url.String());
		return *outDir != NULL ? B_OK : errno;
	}

	status_t CloseDir(SMBCFILE* dir)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionClosedir(fContext)(fContext, dir));
	}

	status_t GetDirectoryEntries(SMBCFILE* dir, struct smbc_dirent* entries,
		int count)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionGetdents(fContext)(fContext, dir,
			entries, count));
	}

	status_t SeekDir(SMBCFILE* dir, off_t offset)
	{
		assert(gGlobalSambaLock.IsLocked());
		return _GetStatus(smbc_getFunctionLseekdir(fContext)(fContext, dir,
			offset));
	}

	status_t GetDirectoryEntry(SMBCFILE* dir, smbc_dirent** outEntry)
	{
		assert(gGlobalSambaLock.IsLocked());
		*outEntry = smbc_getFunctionReaddir(fContext)(fContext, dir);
		if (*outEntry == NULL) {
			if (errno == B_OK)
				return B_ENTRY_NOT_FOUND;
			else
				return errno;
		}
		return B_OK;
	}

private:
	status_t _GetStatus(int smbStatus)
	{
		return smbStatus == 0 ? B_OK : errno;
	}

private:
	SMBCCTX* fContext;
};


} // namespace Smb


#endif // SMBFS_SAMBA_CONTEXT_H
