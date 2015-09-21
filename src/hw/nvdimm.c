// Support for finding and booting from NVDIMM
//
// Copyright (C) 2015  Marc Marí <markmb@redhat.com>
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "std/acpi.h"
#include "util.h"
#include "output.h"
#include "memmap.h"
#include "malloc.h"

void nvdimm_setup(void)
{
    if (!nfit_setup()) {
        dprintf(1, "No NVDIMMs found\n");
        return;
    }

    dprintf(1, "NVDIMMs found\n");
}
