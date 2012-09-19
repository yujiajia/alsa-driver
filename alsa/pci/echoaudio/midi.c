// ****************************************************************************
//
//   Copyright Echo Digital Audio Corporation (c) 1998 - 2004
//   All rights reserved
//   www.echoaudio.com
//
//   This file is part of Echo Digital Audio's generic driver library.
//
//   Echo Digital Audio's generic driver library is free software;
//   you can redistribute it and/or modify it under the terms of
//   the GNU General Public License as published by the Free Software Foundation.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 59 Temple Place - Suite 330, Boston,
//   MA  02111-1307, USA.
//
// ****************************************************************************
//
// Translation from C++ and adaptation for use in ALSA-Driver
// were made by Giuliano Pochini <pochini@shiny.it>
//
// ****************************************************************************


/***************************************************************************************************
*** MIDI lowlevel code
***************************************************************************************************/


/* Start and stop Midi input */
static int enable_midi_input(echoaudio_t *chip, char enable)
{
	DE_MID(("enable_midi_input(%d)\n", enable));

	if (wait_handshake(chip))
		return -EIO;

	if (enable) {
		chip->mtc_state = MIDI_IN_STATE_NORMAL;
		chip->comm_page->flags |= __constant_cpu_to_le32(DSP_FLAG_MIDI_INPUT);
	} else
		chip->comm_page->flags &= ~__constant_cpu_to_le32(DSP_FLAG_MIDI_INPUT);

	clear_handshake(chip);
	return send_vector(chip, DSP_VC_UPDATE_FLAGS);
}



/*
Send a buffer full of MIDI data to the DSP
Returns how many actually written or < 0 on error
*/
static int write_midi(echoaudio_t *chip, u8 *data, int bytes)
{
	if (bytes <= 0)
		return 0;

	if (wait_handshake(chip))
		return -EIO;

	/* Return immediately if HF4 is clear - HF4 indicates that it is safe to write MIDI output data */
	if ((get_dsp_register(chip, CHI32_STATUS_REG) & CHI32_STATUS_REG_HF4) == 0)
		return 0;	//@@@ -EBUSY ?

	/* The first byte contains the number of bytes to send */
	snd_assert(bytes < MIDI_OUT_BUFFER_SIZE, return -EINVAL);

	chip->comm_page->midi_output[0] = bytes;
	memcpy(&chip->comm_page->midi_output[1], data, bytes);
	chip->comm_page->midi_out_free_count = 0;
	clear_handshake(chip);
	send_vector(chip, DSP_VC_MIDI_WRITE);
	DE_MID(("write_midi: %d\n", bytes));
	return bytes;
}



/* Run the state machine for MIDI input data

MIDI time code sync isn't supported by this code right now, but you still need
this state machine to parse the incoming MIDI data stream.  Every time the DSP
sees a 0xF1 byte come in, it adds the DSP sample position to the MIDI data
stream. The DSP sample position is represented as a 32 bit unsigned value,
with the high 16 bits first, followed by the low 16 bits. Since these aren't
real MIDI bytes, the following logic is needed to skip them.
*/
static inline int mtc_process_data(echoaudio_t *chip, short midi_byte)
{
	switch (chip->mtc_state) {
	case MIDI_IN_STATE_NORMAL:
		if (midi_byte == 0xF1)
			chip->mtc_state = MIDI_IN_STATE_TS_HIGH;
		break;
	case MIDI_IN_STATE_TS_HIGH:
		chip->mtc_state = MIDI_IN_STATE_TS_LOW;
		return MIDI_IN_SKIP_DATA;
		break;
	case MIDI_IN_STATE_TS_LOW:
		chip->mtc_state = MIDI_IN_STATE_F1_DATA;
		return MIDI_IN_SKIP_DATA;
		break;
	case MIDI_IN_STATE_F1_DATA:
		chip->mtc_state = MIDI_IN_STATE_NORMAL;
		break;
	}
	return 0;
}



/*
This function is called from the IRQ handler and it reads the midi data
from the DSP's buffer.  It returns the number of bytes received.
*/
static int midi_service_irq(echoaudio_t *chip)
{
	short int count, midi_byte, i, received;

	/* The count is at index 0, followed by actual data */
	count = le16_to_cpu(chip->comm_page->midi_input[0]);

	snd_assert(count < MIDI_IN_BUFFER_SIZE, return 0);

	/* Get the MIDI data from the comm page */
	i = 1;
	received = 0;
	for (i = 1; i <= count; i++) {
		/* Get the MIDI byte */
		midi_byte = le16_to_cpu(chip->comm_page->midi_input[i]);

		/* Parse the incoming MIDI stream. The incoming MIDI data
		consists of MIDI bytes and timestamps for the MIDI time code
		0xF1 bytes. mtc_process_data() is a little state machine that
		parses the stream. If you get MIDI_IN_SKIP_DATA back, then
		this is a timestamp byte, not a MIDI byte, so don't store it
		in the MIDI input buffer. */
		if (mtc_process_data(chip, midi_byte) == MIDI_IN_SKIP_DATA)
			continue;

		chip->midi_buffer[received++] = (u8)midi_byte;
	}

	return received;
}




/***************************************************************************************************
*** MIDI interface
***************************************************************************************************/

static int snd_echo_midi_input_open(snd_rawmidi_substream_t *substream)
{
	echoaudio_t *chip = substream->rmidi->private_data;

	chip->midi_in = substream;
	DE_MID(("rawmidi_iopen\n"));
	return 0;
}



static void snd_echo_midi_input_trigger(snd_rawmidi_substream_t *substream, int up)
{
	echoaudio_t *chip = substream->rmidi->private_data;

	if (up != chip->midi_input_enabled) {
		spin_lock_irq(&chip->lock);
		enable_midi_input(chip, up);
		spin_unlock_irq(&chip->lock);
		chip->midi_input_enabled = up;
	}
}



static int snd_echo_midi_input_close(snd_rawmidi_substream_t *substream)
{
	echoaudio_t *chip = substream->rmidi->private_data;

	chip->midi_in = 0;
	DE_MID(("rawmidi_iclose\n"));
	return 0;
}



static int snd_echo_midi_output_open(snd_rawmidi_substream_t *substream)
{
	echoaudio_t *chip = substream->rmidi->private_data;

	init_timer(&chip->timer);
	chip->tinuse = 0;
	chip->midi_out = substream;
	DE_MID(("rawmidi_oopen\n"));
	return 0;
}



static void snd_echo_midi_output_write(unsigned long data)
{
	echoaudio_t *chip = (echoaudio_t *)data;
	unsigned long flags;
	int bytes, sent, time;
	unsigned char buf[MIDI_OUT_BUFFER_SIZE - 1];

	DE_MID(("snd_echo_midi_output_write\n"));
	/* No interrupts are involved: we have to check at regular intervals
	if the card's output buffer has room for new data. */
	sent = bytes = 0;
	spin_lock_irqsave(&chip->lock, flags);
	if (chip->midi_out && !snd_rawmidi_transmit_empty(chip->midi_out)) {
		bytes = snd_rawmidi_transmit_peek(chip->midi_out, buf, MIDI_OUT_BUFFER_SIZE - 1);
		DE_MID(("Try to send %d bytes...\n", bytes));
		if (bytes > 0) {	//@@ useless ?
			sent = write_midi(chip, buf, bytes);
			if (sent < 0) {
				DE_MID(("write_midi() error %d\n", sent));
				sent = 1000;	/* retry later */
			} else {
				DE_MID(("%d bytes sent\n", sent));
				snd_rawmidi_transmit_ack(chip->midi_out, sent);
			}
		}
	}
	spin_unlock_irqrestore(&chip->lock, flags);

	/* We restart the timer only if there is some data left to send */
	if (!snd_rawmidi_transmit_empty(chip->midi_out) && chip->tinuse) {
		/* The timer will expire slightly after the data has been sent */
		time = (sent << 10) / 3150 + 1;	/* ms */
		chip->timer.expires = ((time * HZ + 999) / 1000) + jiffies;
		add_timer(&chip->timer);
		DE_MID(("Timer armed(%d)\n", ((time * HZ + 999) / 1000)));
	}

	DE_MID(("snd_echo_midi_output_write done\n"));
}



static void snd_echo_midi_output_trigger(snd_rawmidi_substream_t *substream, int up)
{
	echoaudio_t *chip = substream->rmidi->private_data;

	DE_MID(("snd_echo_midi_output_trigger(%d)\n", up));
	spin_lock_irq(&chip->lock);
	if (up) {
		if (!chip->tinuse) {
			init_timer(&chip->timer);
			chip->timer.function = snd_echo_midi_output_write;
			chip->timer.data = (unsigned long)chip;
			chip->tinuse = 1;
		}
	} else {
		if (chip->tinuse) {
			del_timer(&chip->timer);
			chip->tinuse = 0;
			DE_MID(("Timer removed\n"));
		}
	}
	spin_unlock_irq(&chip->lock);

	if (up)
		snd_echo_midi_output_write((unsigned long)chip);
	DE_MID(("rawmidi_otrigger\n"));
}



static int snd_echo_midi_output_close(snd_rawmidi_substream_t *substream)
{
	echoaudio_t *chip = substream->rmidi->private_data;

	chip->midi_out = 0;
	DE_MID(("rawmidi_oclose\n"));
	return 0;
}



static snd_rawmidi_ops_t snd_echo_midi_input = {
	.open = snd_echo_midi_input_open,
	.close = snd_echo_midi_input_close,
	.trigger = snd_echo_midi_input_trigger,
};

static snd_rawmidi_ops_t snd_echo_midi_output = {
	.open = snd_echo_midi_output_open,
	.close = snd_echo_midi_output_close,
	.trigger = snd_echo_midi_output_trigger,
};



/* <--snd_echo_probe() */
static int __devinit snd_echo_midi_create(snd_card_t *card, echoaudio_t *chip)
{
	int err;

	if ((err = snd_rawmidi_new(card, card->shortname, 0, 1, 1, &chip->rmidi)) < 0)
		return err;

	strcpy(chip->rmidi->name, card->shortname);
	chip->rmidi->private_data = chip;

	snd_rawmidi_set_ops(chip->rmidi, SNDRV_RAWMIDI_STREAM_INPUT, &snd_echo_midi_input);
	snd_rawmidi_set_ops(chip->rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT, &snd_echo_midi_output);

	chip->rmidi->info_flags |= SNDRV_RAWMIDI_INFO_OUTPUT | SNDRV_RAWMIDI_INFO_INPUT | SNDRV_RAWMIDI_INFO_DUPLEX;
	DE_INIT(("MIDI ok\n"));
	return 0;
}
