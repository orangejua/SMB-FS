/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#ifndef SMBFS_PROTOCOL_H
#define SMBFS_PROTOCOL_H


namespace Smb {


#define kAssistantSignature "application/x-vnd.haiku-SMB-FS-Assistant"


// ----------------------------------------------------------------------------
//  Messages sent to Assistant
// ----------------------------------------------------------------------------
enum {
	kMsgConfigure          = 0x1001,
	kMsgStatus             = 0x1002,
	kMsgScan               = 0x1003,
	kMsgQuit               = 0x1004
};

// ----------------------------------------------------------------------------
//  Messages sent from Assistant
// ----------------------------------------------------------------------------
enum {
	kMsgScanFinished       = 0x2001,

	kMsgFoundResource      = 0x2002,
		// int8   "type"
		// string "directory url"
		// string "name"
		// string "comment"

	kMsgLostResource       = 0x2003
		// string "directory url"
		// string "name"
};


} // namespace Smb


#endif // SMBFS_PROTOCOL_H
