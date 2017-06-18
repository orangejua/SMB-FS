/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_NODE_TYPE_H
#define SMBFS_NODE_TYPE_H


namespace Smb {


enum NodeType {
	kNetwork        = 1,
	kWorkgroup      = 2,
	kServer         = 3,
	kShare          = 4,
	kShareDirectory = 5,
	kShareFile      = 6
};


enum {
	kInvalidNodeID  = 0,
	kNetworkNodeID  = 1  // (FS root node)
};


inline bool
node_type_valid(NodeType type)
{
	switch (type) {
		case kNetwork:
		case kWorkgroup:
		case kServer:
		case kShare:
		case kShareDirectory:
		case kShareFile:
			return true;

		default:
			return false;
	}
}


} // namespace Smb


#endif // SMBFS_NODE_TYPE_H
