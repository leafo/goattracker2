//
// BME (Blasphemous Multimedia Engine) sound main module
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <jack/jack.h>
#include <jack/transport.h>
#include <jack/midiport.h>

#include <SDL/SDL.h>
#include "bme_main.h"
#include "bme_cfg.h"
#include "bme_win.h"
#include "bme_io.h"
#include "bme_err.h"

typedef jack_default_audio_sample_t sample_t;

// Prototypes
int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength, unsigned channels, int usedirectsound);
void snd_uninit(void);
void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples));


static int snd_initchannels(unsigned channels);

static int snd_initmixer(void);
static void snd_uninitmixer(void);
static void snd_mixdata(Uint8 *dest, unsigned bytes);
static void snd_mixchannels(Sint32 *dest, unsigned samples);
static void snd_mixer(void *userdata, Uint8 *stream, int len);

// Lowlevel mixing functions
static void snd_clearclipbuffer(Sint32 *clipbuffer, unsigned clipsamples);
static void snd_mixchannel(CHANNEL *chptr, Sint32 *dest, unsigned samples);
static void snd_float_postprocess(Sint32 *src, sample_t* dest, unsigned samples);
static void snd_16bit_postprocess(Sint32 *src, Sint16 *dest, unsigned samples);
static void snd_8bit_postprocess(Sint32 *src, Uint8 *dest, unsigned samples);

void (*snd_player)(void) = NULL;
CHANNEL *snd_channel = NULL;
int snd_channels = 0;
int snd_sndinitted = 0;
int snd_bpmcount;
int snd_bpmtempo = 125;
unsigned snd_mixmode;
unsigned snd_mixrate;

static void (*snd_custommixer)(Sint32 *dest, unsigned samples) = NULL;
static unsigned snd_buffersize;
static unsigned snd_samplesize;
static unsigned snd_previouschannels = 0xffffffff;
static int snd_atexit_registered = 0;
static Sint32 *snd_clipbuffer = NULL;
static SDL_AudioSpec desired;
static SDL_AudioSpec obtained;

static int use_jack = 1;

static jack_client_t* client;
static jack_port_t* output_port;
static jack_port_t* midi_input_port;

extern void playtestnote(int note, int ins, int chnnum);

extern int einum;
extern int epchn;

int current_note_on = -1;

int snd_jack_process(jack_nframes_t nframes, void *arg) {
    // poll for midi events
    int i;
    jack_midi_event_t event;
    void* midi_buffer = jack_port_get_buffer(midi_input_port, nframes);
    int num_events = jack_midi_get_event_count(midi_buffer);

    for (i = 0; i < num_events; i++) {
        jack_midi_event_get(&event, midi_buffer, i);
        // printf("size: %u: %u %u %u\n", event.size,
        //     *event.buffer, *(event.buffer+1), *(event.buffer+2));

        if ((*event.buffer & 0xf0) == 0x90) {
            // note on
            unsigned char note = *(event.buffer + 1);
            current_note_on = note;
            playtestnote(note + 72, einum, epchn);
        } else if ((*event.buffer & 0xf0) == 0x80) {
            // note off
            unsigned char note = *(event.buffer + 1);
            if (note == current_note_on) {
                playtestnote(190, einum, epchn);
                current_note_on = -1;
            }
        }
    }

    sample_t* buffer = jack_port_get_buffer(output_port, nframes);
    snd_mixdata((Uint8*)buffer, sizeof(sample_t) * nframes);
    return 0;
}

int snd_init_jack() {
    snd_uninit();

    jack_status_t status;

    client = jack_client_open("goattracker2", JackNoStartServer, &status);
    if (client == 0) {
        fprintf(stderr, "failed to create jack client\n");
        return BME_ERROR;
    }

    snd_mixrate = jack_get_sample_rate(client);

    snd_bpmcount = 0;
    snd_sndinitted = 1;

    snd_mixmode = 0;
    snd_samplesize = 1;

    // force 16 bit
    snd_mixmode |= SIXTEENBIT;
    snd_samplesize <<= 1;

    snd_buffersize = 1880;

    if (!snd_initmixer())
    {
        bme_error = BME_OUT_OF_MEMORY;
        snd_uninit();
        return BME_ERROR;
    }

    jack_set_process_callback(client, snd_jack_process, 0);

    output_port = jack_port_register(client, "playback",
        JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    midi_input_port = jack_port_register(client, "midi_in",
        JACK_DEFAULT_MIDI_TYPE, JackPortIsInput, 0);

    if (jack_activate(client)) {
        fprintf(stderr, "failed to activate\n");
        return BME_ERROR;
    }

    bme_error = BME_OK;
    return BME_OK;
}

int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength, unsigned channels, int usedirectsound)
{

    if (use_jack) {
        return snd_init_jack();
    }

    // Register snd_uninit as an atexit function

    if (!snd_atexit_registered)
    {
        atexit(snd_uninit);
        snd_atexit_registered = 1;
    }

    // If user wants to re-initialize, shutdown first

    snd_uninit();

    // Check for illegal config

    if ((channels < 1) || (!mixrate) || (!bufferlength))
    {
        bme_error = BME_ILLEGAL_CONFIG;
        snd_uninit();
        return BME_ERROR;
    }

    desired.freq = mixrate;
    desired.format = AUDIO_U8;
    if (mixmode & SIXTEENBIT)
    {
        desired.format = AUDIO_S16SYS;
    }
    desired.channels = 1;
    if (mixmode & STEREO) desired.channels = 2;
    desired.samples = bufferlength * mixrate / 1000;
    {
        int bits = 0;

        for (;;)
        {
            desired.samples >>= 1;
            if (!desired.samples) break;
            bits++;
        }
        desired.samples = 1 << bits;
    }

    desired.callback = snd_mixer;
    desired.userdata = NULL;

    // Init tempo count

    snd_bpmcount = 0;

    // (Re)allocate channels if necessary
    if (snd_initchannels(channels) != BME_OK) {
        return BME_ERROR;
    }

    SDL_PauseAudio(1);

    if (SDL_OpenAudio(&desired, &obtained))
    {
        bme_error = BME_OPEN_ERROR;
        snd_uninit();
        return BME_ERROR;
    }
    snd_sndinitted = 1;

    snd_mixmode = 0;
    snd_samplesize = 1;
    if (obtained.channels == 2)
    {
        snd_mixmode |= STEREO;
        snd_samplesize <<= 1;
    }
    if ((obtained.format == AUDIO_S16SYS) ||
       (obtained.format == AUDIO_S16LSB) ||
       (obtained.format == AUDIO_S16MSB))
    {
        snd_mixmode |= SIXTEENBIT;
        snd_samplesize <<= 1;
    }
    snd_buffersize = obtained.size;
    snd_mixrate = obtained.freq;

    // Allocate mixer tables

    if (!snd_initmixer())
    {
        bme_error = BME_OUT_OF_MEMORY;
        snd_uninit();
        return BME_ERROR;
    }

    SDL_PauseAudio(0);

    bme_error = BME_OK;
    return BME_OK;
}

int snd_initchannels(unsigned channels) {
    int c;

    if (snd_previouschannels != channels)
    {
        CHANNEL *chptr;
        if (snd_channel)
        {
            free(snd_channel);
            snd_channel = NULL;
            snd_channels = 0;
        }

        snd_channel = malloc(channels * sizeof(CHANNEL));
        if (!snd_channel)
        {
            bme_error = BME_OUT_OF_MEMORY;
            snd_uninit();
            return BME_ERROR;
        }
        chptr = &snd_channel[0];
        snd_channels = channels;
        snd_previouschannels = channels;

        // Init all channels (no sound played, no sample, mastervolume 64)
        for (c = snd_channels; c > 0; c--)
        {
            chptr->voicemode = VM_OFF;
            chptr->smp = NULL;
            chptr->mastervol = 64;
            chptr++;
        }
    }

    return BME_OK;
}


void snd_uninit(void)
{
    if (!use_jack && snd_sndinitted)
    {
        SDL_CloseAudio();
        snd_sndinitted = 0;
    }
    snd_uninitmixer();
}

void snd_setcustommixer(void (*custommixer)(Sint32 *dest, unsigned samples))
{
    snd_custommixer = custommixer;
}

static int snd_initmixer(void)
{
    snd_uninitmixer();

    if (snd_mixmode & STEREO)
    {
        snd_clipbuffer = malloc((snd_buffersize / snd_samplesize) * sizeof(int) * 2);
    }
    else
    {
        snd_clipbuffer = malloc((snd_buffersize / snd_samplesize) * sizeof(int));
    }
    if (!snd_clipbuffer) return 0;

    return 1;
}

static void snd_uninitmixer(void)
{
    if (snd_clipbuffer)
    {
        free(snd_clipbuffer);
        snd_clipbuffer = NULL;
    }
}

static void snd_mixer(void *userdata, Uint8 *stream, int len)
{
    snd_mixdata(stream, len);
}

static void snd_mixdata(Uint8 *dest, unsigned bytes)
{
    unsigned mixsamples = bytes;
    unsigned clipsamples = bytes;
    Sint32 *clipptr = (Sint32 *)snd_clipbuffer;
    if (snd_mixmode & STEREO) mixsamples >>= 1;
    if (snd_mixmode & SIXTEENBIT)
    {
        clipsamples >>= 1;
        mixsamples >>= 1;
    }

    if (use_jack) {
        clipsamples = bytes / sizeof(sample_t);
        mixsamples = clipsamples;
    }

    snd_clearclipbuffer(snd_clipbuffer, clipsamples);

    if (snd_player) // Must the player be called?
    {
        int musicsamples;

        while(mixsamples)
        {
            if ((!snd_bpmcount) && (snd_player)) // Player still active?
            {
                // Call player
                snd_player();
                // Reset tempocounter
                snd_bpmcount = ((snd_mixrate * 5) >> 1) / snd_bpmtempo;
            }

            musicsamples = mixsamples;
            if (musicsamples > snd_bpmcount) musicsamples = snd_bpmcount;
            snd_bpmcount -= musicsamples;
            if (!snd_custommixer)
            {
                snd_mixchannels(clipptr, musicsamples);
            }
            else
            {
                snd_custommixer(clipptr, musicsamples);
            }
            if (snd_mixmode & STEREO) clipptr += musicsamples * 2;
            else clipptr += musicsamples;
            mixsamples -= musicsamples;
        }
    }
    else
    {
        if (!snd_custommixer)
        {
            snd_mixchannels(clipptr, mixsamples);
        }
        else
        {
            snd_custommixer(clipptr, mixsamples);
        }
    }

    clipptr = (Sint32 *)snd_clipbuffer;

    if (use_jack)
    {
        snd_float_postprocess(clipptr, (sample_t*)dest, clipsamples);
    }
    else if (snd_mixmode & SIXTEENBIT)
    {
        snd_16bit_postprocess(clipptr, (Sint16 *)dest, clipsamples);
    }
    else
    {
        snd_8bit_postprocess(clipptr, dest, clipsamples);
    }
}

static void snd_mixchannels(Sint32 *dest, unsigned samples)
{
    CHANNEL *chptr = &snd_channel[0];
    int c;

    for (c = snd_channels; c; c--)
    {
        snd_mixchannel(chptr, dest, samples);
        chptr++;
    }
}

static void snd_clearclipbuffer(Sint32 *clipbuffer, unsigned clipsamples)
{
    memset(clipbuffer, 0, clipsamples*sizeof(int));
}

static void snd_float_postprocess(Sint32* src, sample_t* dest, unsigned samples) {
    while (samples--)
    {
        int sample = *src++;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        *dest++ = sample / 32768.0;
    }
}

static void snd_16bit_postprocess(Sint32 *src, Sint16 *dest, unsigned samples)
{
    while (samples--)
    {
        int sample = *src++;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        *dest++ = sample;
    }
}

static void snd_8bit_postprocess(Sint32 *src, Uint8 *dest, unsigned samples)
{
    while (samples--)
    {
          int sample = *src++;
          if (sample > 32767) sample = 32767;
          if (sample < -32768) sample = -32768;
          *dest++ = (sample >> 8) + 128;
    }
}

static void snd_mixchannel(CHANNEL *chptr, Sint32 *dest, unsigned samples)
{
    if (chptr->voicemode & VM_ON)
    {
          unsigned freq, intadd, fractadd;

          freq = chptr->freq;
          if (freq > 535232) freq = 535232;
          intadd = freq / snd_mixrate;
          fractadd = (((freq % snd_mixrate) << 16) / snd_mixrate) & 65535;

          if (snd_mixmode & STEREO)
          {
                int leftvol = (((chptr->vol * chptr->mastervol) >> 6) * (255-chptr->panning)) >> 7;
                int rightvol = (((chptr->vol * chptr->mastervol) >> 6) * (chptr->panning)) >> 7;
                if (leftvol > 255) leftvol = 255;
                if (rightvol > 255) rightvol = 255;
                if (leftvol < 0) leftvol = 0;
                if (rightvol < 0) rightvol = 0;

                if (chptr->voicemode & VM_16BIT)
                {
                    Sint16 *pos = (Sint16 *)chptr->pos;
                    Sint16 *end = (Sint16 *)chptr->end;
                    Sint16 *repeat = (Sint16 *)chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * leftvol) >> 8);
                                dest++;
                                *dest = *dest + ((*pos * rightvol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * leftvol) >> 8);
                                dest++;
                                *dest = *dest + ((*pos * rightvol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
                else
                {
                    Sint8 *pos = (Sint8 *)chptr->pos;
                    Sint8 *end = chptr->end;
                    Sint8 *repeat = chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * leftvol);
                                dest++;
                                *dest = *dest + (*pos * rightvol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * leftvol);
                                dest++;
                                *dest = *dest + (*pos * rightvol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
          }
          else
          {
                int vol = ((chptr->vol * chptr->mastervol) >> 6);
                if (vol > 255) vol = 255;
                if (vol < 0) vol = 0;

                if (chptr->voicemode & VM_16BIT)
                {
                    Sint16 *pos = (Sint16 *)chptr->pos;
                    Sint16 *end = (Sint16 *)chptr->end;
                    Sint16 *repeat = (Sint16 *)chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * vol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + ((*pos * vol) >> 8);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
                else
                {
                    Sint8 *pos = (Sint8 *)chptr->pos;
                    Sint8 *end = chptr->end;
                    Sint8 *repeat = chptr->repeat;

                    if (chptr->voicemode & VM_LOOP)
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * vol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                while (pos >= end) pos -= (end - repeat);
                          }
                    }
                    else
                    {
                          while (samples--)
                          {
                                *dest = *dest + (*pos * vol);
                                dest++;
                                chptr->fractpos += fractadd;
                                if (chptr->fractpos > 65535)
                                {
                                    chptr->fractpos &= 65535;
                                    pos++;
                                }
                                pos += intadd;
                                if (pos >= end)
                                {
                                    chptr->voicemode &= ~VM_ON;
                                    break;
                                }
                          }
                    }
                    chptr->pos = (Sint8 *)pos;
                }
          }
    }
}
