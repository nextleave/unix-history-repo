/* $FreeBSD$ */
/*-
 * Copyright (c) 2008 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _USB_REVISION_H_
#define	_USB_REVISION_H_

/*
 * The "USB_SPEED" macros defines all the supported USB speeds.
 */
enum usb_dev_speed {
	USB_SPEED_VARIABLE,
	USB_SPEED_LOW,
	USB_SPEED_FULL,
	USB_SPEED_HIGH,
	USB_SPEED_SUPER,
};
#define	USB_SPEED_MAX	(USB_SPEED_SUPER+1)

/*
 * The "USB_REV" macros defines all the supported USB revisions.
 */
enum usb_revision {
	USB_REV_UNKNOWN,
	USB_REV_PRE_1_0,
	USB_REV_1_0,
	USB_REV_1_1,
	USB_REV_2_0,
	USB_REV_2_5,
	USB_REV_3_0
};
#define	USB_REV_MAX	(USB_REV_3_0+1)

/*
 * Supported host contoller modes.
 */
enum usb_hc_mode {
	USB_MODE_HOST,		/* initiates transfers */
	USB_MODE_DEVICE,	/* bus transfer target */
	USB_MODE_DUAL		/* can be host or device */
};
#define	USB_MODE_MAX	(USB_MODE_DUAL+1)

/*
 * The "USB_MODE" macros defines all the supported device states.
 */
enum usb_dev_state {
	USB_STATE_DETACHED,
	USB_STATE_ATTACHED,
	USB_STATE_POWERED,
	USB_STATE_ADDRESSED,
	USB_STATE_CONFIGURED,
};
#define	USB_STATE_MAX	(USB_STATE_CONFIGURED+1)
#endif					/* _USB_REVISION_H_ */
