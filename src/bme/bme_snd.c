//
// BME (Blasphemous Multimedia Engine) sound main module
//

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef USE_JACK
#  include <jack/jack.h>
#  include <jack/transport.h>
#endif

#ifdef USE_MIDI_INPUT
#  include <rtmidi/rtmidi_c.h>
#endif

#include <SDL.h>
#include "bme_main.h"
#include "bme_cfg.h"
#include "bme_win.h"
#include "bme_io.h"
#include "bme_err.h"

#ifdef USE_JACK
typedef jack_default_audio_sample_t sample_t;
#endif

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
#ifdef USE_JACK
static void snd_jack_postprocess(Sint32 *src, sample_t* dest, unsigned samples);
#endif
static void snd_float_postprocess(Sint32 *src, float* dest, unsigned samples);
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
static unsigned snd_framesize;
static unsigned snd_previouschannels = 0xffffffff;
static int snd_atexit_registered = 0;
static Sint32 *snd_clipbuffer = NULL;
static SDL_AudioSpec desired;
static SDL_AudioSpec obtained;

#ifdef USE_JACK
static int use_jack = 1;
static int use_jack_audio = 0;

static jack_client_t* client;
static jack_port_t* output_port;
#endif

#ifdef USE_MIDI_INPUT
RtMidiInPtr midi_device = NULL;
#endif

void playtestnote(int note, int ins, int chnnum);
void insertnote(int newnote);

#define VISIBLEPATTROWS 31

extern int einum;
extern int epchn;
extern int epview;
extern int eppos;

int current_note_on = -1;

#ifdef USE_JACK
int snd_jack_process(jack_nframes_t nframes, void *arg) {
    if (use_jack_audio) {
        sample_t* buffer = jack_port_get_buffer(output_port, nframes);
        snd_mixdata((Uint8*)buffer, sizeof(sample_t) * nframes);
    }
    return 0;
}

int snd_init_jack() {
    jack_status_t status;

    client = jack_client_open("goattracker2", JackNoStartServer, &status);
    if (client == 0) {
        fprintf(stderr, "failed to create jack client\n");
        return BME_ERROR;
    }

    if (use_jack_audio) {
        snd_mixrate = jack_get_sample_rate(client);

        snd_bpmcount = 0;
        snd_sndinitted = 1;

        // force 16 bit
        snd_mixmode = SIXTEENBIT;
        snd_framesize = 2;

        snd_buffersize = 1880;

        if (!snd_initmixer())
        {
            bme_error = BME_OUT_OF_MEMORY;
            snd_uninit();
            return BME_ERROR;
        }
    }


    jack_set_process_callback(client, snd_jack_process, 0);

    if (use_jack_audio) {
        output_port = jack_port_register(client, "playback",
            JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    }

    if (jack_activate(client)) {
        fprintf(stderr, "failed to activate\n");
        return BME_ERROR;
    }

    bme_error = BME_OK;
    return BME_OK;
}
#endif

#ifdef USE_MIDI_INPUT
void snd_midi_process(double timeStamp, const unsigned char *message, size_t messageSize, void *userData) {
    int i;

    for (i = 0; i < messageSize; i++) {
        // printf("size: %u: %u %u %u\n", messageSize,
        //     *message, *(message+1), *(message+2));

        unsigned char midi_cmd = message[i];
        if ((midi_cmd & 0xf0) == 0x90) {
            // note on
            unsigned char note = message[i+1];
            current_note_on = note;
            insertnote(note + 72);
            epview = eppos-VISIBLEPATTROWS/2;
        } else if ((midi_cmd & 0xf0) == 0x80) {
            // note off
            unsigned char note = message[i+1];
            if (note == current_note_on) {
                playtestnote(190, einum, epchn); // off note
                current_note_on = -1;
            }
        }
    }
}

int snd_init_midi() {
    RtMidiInPtr midi_device = rtmidi_in_create(RTMIDI_API_UNSPECIFIED, "goattracker2", 100);
    if (!midi_device->ok) {
        fprintf(stderr, "failed to activate midi: %s\n", midi_device->msg);
        return BME_ERROR;
    }

    rtmidi_open_port(midi_device, 0, "midi_in");
    if (!midi_device->ok) {
        fprintf(stderr, "failed to open port: %s\n", midi_device->msg);
        return BME_ERROR;
    }

    rtmidi_in_set_callback(midi_device, snd_midi_process, NULL);
    if (!midi_device->ok) {
        fprintf(stderr, "failed to set midi callback: %s\n", midi_device->msg);
        return BME_ERROR;
    }

    bme_error = BME_OK;
    return BME_OK;
}
#endif

int snd_init(unsigned mixrate, unsigned mixmode, unsigned bufferlength, unsigned channels, int usedirectsound)
{
    // If user wants to re-initialize, shutdown first

    snd_uninit();

#ifdef USE_MIDI_INPUT
    snd_init_midi();
#endif

#ifdef USE_JACK
    if (use_jack) {
        snd_init_jack();
        if (use_jack_audio) return BME_OK;
    }
#endif
    // Register snd_uninit as an atexit function

    if (!snd_atexit_registered)
    {
        atexit(snd_uninit);
        snd_atexit_registered = 1;
    }

    // Check for illegal config

    if ((!mixrate) || (!bufferlength))
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

    SDL_PauseAudio(1);

    if (SDL_OpenAudio(&desired, &obtained))
    {
        bme_error = BME_OPEN_ERROR;
        snd_uninit();
        return BME_ERROR;
    }
    snd_sndinitted = 1;

    snd_mixmode = 0;
    snd_framesize = 1;

    if (obtained.channels == 2)
    {
        snd_mixmode |= STEREO;
        snd_framesize <<= 1;
    }

    // (Re)allocate channels if necessary
    if (snd_initchannels(obtained.channels) != BME_OK) {
        return BME_ERROR;
    }

    if ((SDL_AUDIO_BITSIZE(obtained.format) == 16) &&
       SDL_AUDIO_ISSIGNED(obtained.format) &&
       SDL_AUDIO_ISINT(obtained.format))
    {
        snd_mixmode |= SIXTEENBIT;
        snd_framesize <<= 1;
    }
    else if ((SDL_AUDIO_BITSIZE(obtained.format) == 32) &&
       SDL_AUDIO_ISSIGNED(obtained.format) &&
       SDL_AUDIO_ISFLOAT(obtained.format))
    {
        snd_mixmode |= FLOAT32BIT;
        snd_framesize <<= 2;
    }
    else
    {
        bme_error = BME_SOUND_ERROR;
        snd_uninit();
        return BME_ERROR;
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
    if (snd_sndinitted
#ifdef USE_JACK
        && !use_jack_audio
#endif
        )
    {
        SDL_CloseAudio();
        snd_sndinitted = 0;
    }
    snd_uninitmixer();
#ifdef USE_MIDI_INPUT
    if (midi_device != NULL)
        rtmidi_in_free(midi_device);
#endif
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
        snd_clipbuffer = malloc((snd_buffersize / snd_framesize) * sizeof(Sint32) * 2);
    }
    else
    {
        snd_clipbuffer = malloc((snd_buffersize / snd_framesize) * sizeof(Sint32));
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
    unsigned mixsamples = bytes / snd_framesize;
    unsigned clipsamples = mixsamples;
    if (snd_mixmode & STEREO) clipsamples <<= 1;
    Sint32 *clipptr = (Sint32 *)snd_clipbuffer;
#ifdef USE_JACK
    if (use_jack_audio) {
        clipsamples = bytes / sizeof(sample_t);
        mixsamples = clipsamples;
    }
#endif
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
#ifdef USE_JACK
    if (use_jack_audio)
    {
        snd_jack_postprocess(clipptr, (sample_t*)dest, clipsamples);
    }
    else
#endif
    if (snd_mixmode & FLOAT32BIT)
    {
        snd_float_postprocess(clipptr, (float *)dest, clipsamples);
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
    memset(clipbuffer, 0, clipsamples*sizeof(Sint32));
}

#ifdef USE_JACK
static void snd_jack_postprocess(Sint32* src, sample_t* dest, unsigned samples) {
    while (samples--)
    {
        int sample = *src++;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        *dest++ = sample / 32768.0;
    }
}
#endif

static void snd_float_postprocess(Sint32* src, float* dest, unsigned samples) {
    while (samples--)
    {
        int sample = *src++;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        *dest++ = sample / 32768.0f;
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
