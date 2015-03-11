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

#include "libusb.h"

void usb_detach_kernel_driver(int vid, int pid)
{
  libusb_context *ctx;
  if (libusb_init(&ctx) != 0)
    return;
  libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, vid, pid);
  if (dev == NULL)
    return;
  libusb_detach_kernel_driver(dev, 0);
  libusb_exit(ctx);
}

void usb_reset_device(int vid, int pid)
{
  libusb_context *ctx;
  if (libusb_init(&ctx) != 0)
    return;
  libusb_device_handle *dev = libusb_open_device_with_vid_pid(ctx, vid, pid);
  if (dev == NULL)
    return;
  libusb_reset_device(dev);
  libusb_exit(ctx);
}

