/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "SambaContext.h"

#include <stdio.h>


using namespace Smb;


namespace Smb {
BLocker gGlobalSambaLock;
}


void
Smb::get_authentication(const char* server, const char* share,
	char*, int,
	char* username, int usernameMaxLength,
	char* password, int passwordMaxLength)
{
	// TODO
	(void)server;
	(void)share;
	(void)passwordMaxLength;
	strlcpy(username, "guest", usernameMaxLength);
	password[0] = '\0';
}
