/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/pci.h"

#include "drivers/e1000/e1000.h"
#include "drivers/hda/hda.h"

#include "kernel/log/log.h"
#include "kernel/printf/printf.h"

/*
 * The drivers this platform knows about. A new PCI driver is added by
 * writing a hal_pci_driver_t in its drivers/<name>/ directory and
 * listing it here (docs/pci.md).
 */
static const hal_pci_driver_t *const drivers[] = {
    &e1000_pci_driver,
    &hda_pci_driver,
};

static int matches(const hal_pci_driver_t *driver, const hal_pci_device_t *device)
{
    if (driver->vendor_id != 0)
        return driver->vendor_id == device->vendor_id && driver->device_id == device->device_id;

    return driver->class_code == device->class_code && driver->subclass == device->subclass;
}

unsigned int hal_pci_probe_drivers(void)
{
    unsigned int taken = 0;

    for (unsigned int i = 0; i < hal_pci_device_count(); i++)
    {
        hal_pci_device_t device;

        hal_pci_device(i, &device);

        for (unsigned int d = 0; d < sizeof(drivers) / sizeof(drivers[0]); d++)
        {
            if (drivers[d] == 0 || !matches(drivers[d], &device))
                continue;

            if (drivers[d]->probe(&device) == 0)
            {
                kernel_printf("PCI %x:%x.%x: driver %s\n", device.bus, device.device,
                              device.function, drivers[d]->name);
                taken++;

                break;
            }
        }
    }

    return taken;
}
