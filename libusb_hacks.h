/*
 * Copyright (C) 2015 Swift Navigation Inc.
 *
 * Contacts: Gareth McMullin <gareth@swiftnav.com>
 *
 * This source is subject to the license found in the file 'LICENSE' which must
 * be be distributed together with this source. All other rights reserved.
 *
 * THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR PURPOSE.
 */
#ifndef __LIBUSB_HACKS_H
#define __LIBUSB_HACKS_H

void usb_detach_kernel_driver(int vid, int pid);
void usb_reset_device(int vid, int pid);

#endif

