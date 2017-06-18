/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_NODE_H
#define SMBFS_NODE_H

#include <String.h>
#include <SupportDefs.h>

#include "NodeDefs.h"


struct dirent;
struct stat;


namespace Smb {


class SambaContext;
class Volume;


/*! Interface for all node types. Defines methods for all node-specific
    operations.
*/
class Node {
public:
								Node(const BString& url, size_t nameLength,
									Volume* volume, SambaContext* context);

								Node(ino_t id, const BString& url,
									const char* name, Volume* volume,
									SambaContext* context);

								Node(const Node& prototype,
									const BString& newURL, size_t nameLength);

	virtual						~Node();

			const BString&		URL() const;
			const char*			Name() const;
			ino_t				ID() const;

	virtual	NodeType			Type() const = 0;
			mode_t				StatType() const;

	virtual	void				Delete(bool removed, bool reenter);
			void				MovedTo(const BString& newURL);

// ----- FS hooks: all nodes --------------------------------------------------
	virtual	status_t			ReadStat(struct stat* destination) = 0;
	virtual	status_t			WriteStat(const struct stat* source,
									uint32 statMask) = 0;

	virtual	status_t			Open(int mode, void** outCookie) = 0;
	virtual	status_t			Close(void* cookie) = 0;
	virtual	status_t			FreeCookie(void* cookie) = 0;

// ----- FS hooks: file nodes -------------------------------------------------
	virtual	status_t			Read(void* cookie, off_t offset, void* buffer,
									size_t* length) = 0;
	virtual	status_t			Write(void* cookie, off_t offset,
									const void* buffer, size_t* length) = 0;

// ----- FS hooks: directory nodes --------------------------------------------
	virtual	status_t			Lookup(const char* name, ino_t* outNodeID) = 0;

	virtual	status_t			Create(const char* name, int openMode,
									int permissions, void** outCookie,
									ino_t* newNodeID) = 0;
	virtual	status_t			Remove(const char* name) = 0;
	virtual	status_t			Rename(const char* fromName, Node* toDir,
									const char* toName) = 0;

	virtual	status_t			OpenDir(void** outCookie) = 0;
	virtual	status_t			CloseDir(void* cookie) = 0;
	virtual	status_t			ReadDir(void* cookie, struct dirent* buffer,
									size_t bufferSize, uint32* num) = 0;
	virtual	status_t			FreeDirCookie(void* cookie) = 0;
	virtual	status_t			RewindDirCookie(void* cookie) = 0;

	virtual	status_t			CreateDir(const char* name,
									int permissions) = 0;
	virtual	status_t			RemoveDir(const char* name) = 0;

protected:
			BString				_EntryURL(const char* entryName);

protected:

			Volume*	const		fVolume;
			SambaContext* const	fSambaContext;

			const BString		fURL;
			const char*	const	fName;
			const ino_t			fID;
};


} // namespace Smb


#endif // SMBFS_NODE_H
