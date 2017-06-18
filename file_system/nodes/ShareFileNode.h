/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_SHARE_FILE_NODE_H
#define SMBFS_SHARE_FILE_NODE_H

#include <SupportDefs.h>

#include "ShareNode.h"


namespace Smb {


/*! File node inside an SMB share.
*/
class ShareFileNode : public ShareNode  {
public:
								ShareFileNode(const BString& url,
									size_t nameLength, Volume* volume,
									SambaContext* context);
								ShareFileNode(
									const ShareFileNode& prototype,
									const BString& newURL, size_t nameLength);
	virtual						~ShareFileNode();

	virtual	NodeType			Type() const;

// --- only for files ---------------------------------------------------------
	virtual	status_t			Read(void* cookie, off_t offset, void* buffer,
									size_t* length);
	virtual	status_t			Write(void* cookie, off_t offset,
									const void* buffer, size_t* length);

// --- only for directories, all these fail on this node ----------------------
	virtual	status_t			Lookup(const char* name, ino_t* outNodeID);
	virtual	status_t			Create(const char* name, int openMode,
									int permissions, void** outCookie,
									ino_t* newNodeID);
	virtual	status_t			Remove(const char* name);
	virtual	status_t			Rename(const char* fromName, Node* toDir,
									const char* toName);
	virtual	status_t			OpenDir(void** outCookie);
	virtual	status_t			CloseDir(void* cookie);
	virtual	status_t			ReadDir(void* cookie, struct dirent* buffer,
									size_t bufferSize, uint32* num);
	virtual	status_t			FreeDirCookie(void* cookie);
	virtual	status_t			RewindDirCookie(void* cookie);
	virtual	status_t			CreateDir(const char* name, int permissions);
	virtual	status_t			RemoveDir(const char* name);
};


} // namespace Smb


#endif // SMBFS_SHARE_FILE_NODE_H
