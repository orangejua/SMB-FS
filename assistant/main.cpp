/*
 * Copyright 2017 Julian Harnath <julian.harnath@rwth-aachen.de>
 * All rights reserved. Distributed under the terms of the MIT license.
 */

#include "Assistant.h"


using namespace Smb;


int
main()
{
	Assistant* assistant = new Assistant();
	assistant->Run();

	delete assistant;
	return 0;
}
