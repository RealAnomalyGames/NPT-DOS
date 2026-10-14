#ifndef NPT_DOS_PCI_H
#define NPT_DOS_PCI_H

#include <stdint.h>

/*
 * PCI configuration-space enumeration.
 *
 * This milestone only discovers AHCI controllers and reads
 * their configuration registers. It does not access disk data.
 */

void pci_initialize(void);

#endif
