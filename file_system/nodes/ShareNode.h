/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_SHARE_NODE_H
#define SMBFS_SHARE_NODE_H

#include <SupportDefs.h>

#include "Node.h"


namespace Smb {


/*! A node of an SMB share (directory or file).
*/
class ShareNode : public Node {
public:
								ShareNode(const BString& url,
									size_t nameLength, Volume* volume,
									SambaContext* context);
								ShareNode(const ShareNode& prototype,
									const BString& newURL, size_t nameLength);

	virtual						~ShareNode();

	virtual	status_t			ReadStat(struct stat* destination);
	virtual	status_t			WriteStat(const struct stat* source,
									uint32 statMask);

	virtual	status_t			Open(int mode, void** outCookie);
	virtual	status_t			Close(void* cookie);
	virtual	status_t			FreeCookie(void* cookie);
};


} // namespace Smb


#endif // SMBFS_SHARE_NODE_H
