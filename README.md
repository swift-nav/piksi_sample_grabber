# Dependencies
#### libftdi1
Dependency for sample_grabber
http://www.intra2net.com/en/developer/libftdi/download.php

libftdi1 has the following dependencies:

libusb-1.0-0-dev: http://libusb.info/

libconfuse: http://www.nongnu.org/confuse/

We recommend installing these from source from the links. You may need to restart your computer to make sure these kernel modules are running before using the binaries.

#### FTDI D2XX drivers
Dependency for set_uart_mode and set_fifo_mode
http://www.ftdichip.com/Drivers/D2XX.htm


# Building
To build the binaries run the following command in the root directory of the repository after installing the dependencies:

    $ make

# Using the Piksi Sample Grabber
In order to use sample grabber, you must first set your Piksi into FIFO mode instead of the default UART mode. To do this, use set_fifo_mode.

    $ ./set_fifo_mode -v

You may need to unload the FTDI Virtual Com Port kernel module before doing this. In Linux, this is done via the following:

    $ sudo rmmod ftdi_sio
    
If using OSX, this is done via:

    $ sudo kextunload -b com.apple.driver.AppleUSBFTDI

Unplug and replug your Piksi to reset the FTDI chip. Now collect samples by running sample_grabber. Note that you'll need to add the new USB Product ID to your USB permissions in /etc/udev/rules.d (if using Linux), or use sudo on the following commands.

    $ sudo ./sample_grabber -v -s 100M mysamples.dat
    
After you're finished collecting samples, put your Piksi back in UART mode via the following, and then unplugging and replugging the Piksi:

    $ sudo ./set_uart_mode -v

### Note : Simultaneous sampling from multiple Piksies
In order to simultaneously sample from multiple Piksies, they must be assigned different USB product ID's. The FTDI custom product ID for Piksi is 0x8398, and this is the ID Piksi is assigned by set_fifo_mode by default. If you want to simultaneously sample from another Piksi, it must be assigned a different product ID. To do this, use the -i option with set_fifo_mode, e.g. :

    $ ./set_fifo_mode -v -i 0x8399

This will assign the Piksi the product ID 0x8399, instead of the default 0x8398. Note that you'll need to now add the new USB Product ID to your USB permissions in /etc/udev/rules.d, or use sudo on the following commands. To collect samples from this Piksi, use sample_grabber with the -i option, e.g. :

    $ sudo ./sample_grabber -v -s 100M -i 0x8399 my_sample_file.dat

After you finish sampling with this Piksi, you'll want to set it back into UART mode for normal operation. set_uart_mode looks for a device with our custom USB product ID 0x8398 by default, so you'll need to supply the -i option to set_uart_mode to reset the Piksi with the modified id of 0x8399, e.g. :

    $ sudo ./set_uart_mode -v -i 0x8399

# Binaries
#### sample_grabber
Receives an arbitrary number of raw samples from the Piksi,
using the onboard FT232H in FIFO mode. The FT232H on the Piksimust be set in FIFO mode before sample_grabber can be used - run set_fifo_mode to do this. After finishing using sample_grabber, use set_uart_mode to set the FT232H on the Piksi in UART mode for normal operation.

#### set_fifo_mode
Writes settings to EEPROM attached to FT232H for synchronous FIFO mode in order to stream raw samples from the RF frontend through the FPGA. Must be used before running sample_grabber.

#### set_uart_mode
Erases EEPROM attached to FT232H to set the FT232H in UART mode for normal operation. Should be used after running sample_grabber.
