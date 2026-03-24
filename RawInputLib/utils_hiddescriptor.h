#pragma once

#include <hidsdi.h>   // HIDP_PREPARSED_DATA, HIDP_CAPS, etc.

bool ReconstructDescriptor(const PHIDP_PREPARSED_DATA ppd, std::vector<UCHAR>& outDesc);