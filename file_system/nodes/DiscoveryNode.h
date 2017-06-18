/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_DISCOVERY_NODE_H
#define SMBFS_DISCOVERY_NODE_H

#include <Locker.h>
#include <ObjectList.h>
#include <SupportDefs.h>

#include <sys/stat.h>

#include <vector>

#include "Node.h"


namespace Smb {


/*! Nodes which are dynamically filled with workgroups/server/shares discovered
    on the network.
    Disallows any write operations.
*/
class DiscoveryNode : public Node {
public:
								DiscoveryNode(Volume* volume,
									SambaContext* context);
									// for creating the network (FS root) node

								DiscoveryNode(const BString& url,
									size_t nameLength, const BString& comment,
									NodeType type, DiscoveryNode* parent);

								DiscoveryNode(const char* name,
									DiscoveryNode* prototype);

	virtual						~DiscoveryNode();

	virtual	NodeType			Type() const;

	Node*						AddEntry(NodeType type, const BString& name,
									const BString& comment);
			ino_t				RemoveEntry(const BString& name);

// --- FS hooks ---------------------------------------------------------------
	virtual	status_t			ReadStat(struct stat* destination);

	virtual	status_t			Open(int mode, void** outCookie);
	virtual	status_t			Close(void* cookie);
	virtual	status_t			FreeCookie(void* cookie);

	virtual	status_t			Lookup(const char* name, ino_t* outNodeID);

	virtual	status_t			OpenDir(void** outCookie);
	virtual	status_t			CloseDir(void* cookie);
	virtual	status_t			ReadDir(void* cookie, struct dirent* buffer,
									size_t bufferSize, uint32* num);
	virtual	status_t			FreeDirCookie(void* cookie);
	virtual	status_t			RewindDirCookie(void* cookie);

// --- only for files, all these fail on this node (discovery is always dir) --
	virtual	status_t			Read(void* cookie, off_t offset, void* buffer,
									size_t* length);
	virtual	status_t			Write(void* cookie, off_t offset,
									const void* buffer, size_t* length);

// --- write operations, all disallowed on discovery nodes --------------------
	virtual	status_t			WriteStat(const struct stat* source,
									uint32 statMask);
	virtual	status_t			Create(const char* name, int openMode,
									int permissions, void** outCookie,
									ino_t* newNodeID);
	virtual	status_t			Remove(const char* name);
	virtual	status_t			Rename(const char* fromName, Node* toDir,
									const char* toName);
	virtual	status_t			CreateDir(const char* name,
									int permissions);
	virtual	status_t			RemoveDir(const char* name);

private:
	struct Cookie;

	struct Entry {
								Entry(Node* node);
								~Entry();

			Node*				fNode;
			bool				fWasRemoved;
	};
	typedef std::vector<Entry> Entries;

private:
			status_t			_GetDirEntry(Entries::iterator it,
									struct dirent** destination,
									size_t* bufferBytesLeft);
			bool				_HasEntry(const char* name);

			void				_FillStat();
			void				_SetStatTimeToNow();
			void				_AddDotDirEntries();

			BString				_EntryURL(const char* entryName);

private:
			NodeType			fType;
			struct stat*		fStat;
			BLocker				fLock;
			uint32				fDirOpenCount;
			DiscoveryNode*		fParent;
			BString				fComment;

			Entries				fEntries;
};


} // namespace Smb


#endif // SMBFS_DISCOVERY_NODE_H
