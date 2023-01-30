#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/atom/util.h"
#include "lv2/lv2plug.in/ns/ext/log/log.h"
#include "lv2/lv2plug.in/ns/ext/log/logger.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include "lv2/lv2plug.in/ns/ext/state/state.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/worker/worker.h"
#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define PLUGIN_URI "http://moddevices.com/plugins/mod-devel/mod-auto-input-selector"
#define PLUGIN__loadHWJackValues  PLUGIN_URI "#loadHWJackValues"

// These hardcoded paths are very, very dirty
// not only will this plugin ONLY work on the Dwarf, it might even be HW rev. dependant
// although as of writing (2022) this is not the case
// THESE NUMBERS ARE SWAPPED AS MOD-UI SWAPS THE I/O NUMBERS TOO!!
#define GPIO_INP_JACK_2_NUM     "77"
#define GPIO_INP_JACK_1_NUM     "64"
#define GPIO_OUTP_JACK_2_NUM    "63"
#define GPIO_OUTP_JACK_1_NUM    "62"

#define JACK_GPIO_PATH          "/sys/class/gpio/gpio"
#define JACK_GPIO_VALUE_FILE    "/value"
#define GPIO_INP_JACK_1		    JACK_GPIO_PATH GPIO_INP_JACK_1_NUM JACK_GPIO_VALUE_FILE
#define GPIO_INP_JACK_2		    JACK_GPIO_PATH GPIO_INP_JACK_2_NUM JACK_GPIO_VALUE_FILE
#define GPIO_OUTP_JACK_1	    JACK_GPIO_PATH GPIO_OUTP_JACK_1_NUM JACK_GPIO_VALUE_FILE
#define GPIO_OUTP_JACK_2	    JACK_GPIO_PATH GPIO_OUTP_JACK_2_NUM JACK_GPIO_VALUE_FILE

#define REFRESH_TIME_S      2

typedef enum {
	In1    = 0,
    In2,
    Out1,
    Out2,
    OutMono,
    SummedGain
} PortIndex;

typedef enum {
    OUTP_1    = 0,
    OUTP_2,
    OUTP_12
} OutPort;

typedef enum {
    I1_O1    = 0,
    I2_O1,
    I12_O1,
    I1_O2,
    I2_O2,
    I12_O2,
    I1_O12,
    I2_O12,
    I12_O12,
    NO_JACKS
} PatchState;

typedef struct {
    LV2_URID plugin;
    LV2_URID atom_Path;
    LV2_URID atom_Sequence;
    LV2_URID atom_URID;
    LV2_URID atom_eventTransfer;
    LV2_URID atom_String;
    LV2_URID atom_Int;
    LV2_URID midi_Event;
    LV2_URID patch_Get;
    LV2_URID patch_Set;
    LV2_URID patch_Put;
    LV2_URID patch_body;
    LV2_URID patch_subject;
    LV2_URID patch_property;
    LV2_URID patch_value;
    LV2_URID state_StateChanged;
    LV2_URID uri_loadHWJackValues;
} URIs;

typedef struct {
    // Features
    LV2_URID_Map*        map;
    LV2_Worker_Schedule* schedule;
    LV2_Log_Log*         log;

    // Forge for creating atoms
    LV2_Atom_Forge forge;
    // Logger convenience API
    LV2_Log_Logger logger;
    // URIs
    URIs uris;

    float* in1;
    float* in2;
    float* out1;
    float* out2;
    float* outMono;

    float* summedGain;

    int inp_jack_1_value;
    int inp_jack_2_value;
    int stereo_jack_plugged;

    FILE* inp_jack_1_file;
    FILE* inp_jack_2_file;

    uint32_t samplerate;
    uint32_t refresh_counter;
} Plugin;

static inline void
map_uris(LV2_URID_Map* map, URIs* uris)
{
    uris->plugin = map->map(map->handle, PLUGIN_URI);

    uris->atom_Path          = map->map(map->handle, LV2_ATOM__Path);
    uris->atom_Sequence      = map->map(map->handle, LV2_ATOM__Sequence);
    uris->atom_URID          = map->map(map->handle, LV2_ATOM__URID);
    uris->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
    uris->atom_String        = map->map(map->handle, LV2_ATOM__String);
    uris->atom_Int           = map->map(map->handle, LV2_ATOM__Int);
    uris->midi_Event         = map->map(map->handle, LV2_MIDI__MidiEvent);
    uris->patch_Get          = map->map(map->handle, LV2_PATCH__Get);
    uris->patch_Set          = map->map(map->handle, LV2_PATCH__Set);
    uris->patch_Put          = map->map(map->handle, LV2_PATCH__Put);
    uris->patch_body         = map->map(map->handle, LV2_PATCH__body);
    uris->patch_subject      = map->map(map->handle, LV2_PATCH__subject);
    uris->patch_property     = map->map(map->handle, LV2_PATCH__property);
    uris->patch_value        = map->map(map->handle, LV2_PATCH__value);
    uris->state_StateChanged = map->map(map->handle, LV2_STATE__StateChanged);

    uris->uri_loadHWJackValues = map->map(map->handle, PLUGIN__loadHWJackValues);
}

static void
ReadHWJackValues(Plugin* self)
{
    self->inp_jack_1_file = fopen (GPIO_INP_JACK_1, "r");
    if (self->inp_jack_1_file == NULL) {
        lv2_log_error(&self->logger, "Missing input 1 GPIO file\n");
    }
    else {
        fscanf (self->inp_jack_1_file, "%d", &self->inp_jack_1_value);
        fclose(self->inp_jack_1_file);
    }

    self->inp_jack_2_file = fopen (GPIO_INP_JACK_2, "r");
    if (self->inp_jack_2_file == NULL) {
        lv2_log_error(&self->logger, "Missing input 2 GPIO file\n");
    }
    else {
        fscanf (self->inp_jack_2_file, "%d", &self->inp_jack_2_value);
        fclose(self->inp_jack_2_file);
    }

    if ((self->inp_jack_1_value == 1) && (self->inp_jack_2_value == 1))
        self->stereo_jack_plugged = 1;
    else
        self->stereo_jack_plugged = 0;
}

/**
   Do work in a non-realtime thread.
   This is called for every piece of work scheduled in the audio thread using
   self->schedule->schedule_work().  A reply can be sent back to the audio
   thread using the provided respond function.
*/
static LV2_Worker_Status
work(LV2_Handle                  instance,
     LV2_Worker_Respond_Function respond,
     LV2_Worker_Respond_Handle   handle,
     uint32_t                    size,
     const void*                 data)
{
    Plugin*        self = (Plugin*)instance;
    const LV2_Atom* atom = (const LV2_Atom*)data;

    if (atom->type == self->uris.uri_loadHWJackValues) {
        ReadHWJackValues(self);
    }

    return LV2_WORKER_SUCCESS;
}

/**
   Handle a response from work() in the audio thread.
   When running normally, this will be called by the host after run().  When
   freewheeling, this will be called immediately at the point the work was
   scheduled.
*/
static LV2_Worker_Status
work_response(LV2_Handle  instance,
              uint32_t    size,
              const void* data)
{

    return LV2_WORKER_SUCCESS;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	Plugin* self = (Plugin*)instance;

	switch ((PortIndex)port) {
	case In1:
		self->in1 = (float*)data;
		break;
    case In2:
        self->in2 = (float*)data;
        break;
    case Out1:
        self->out1 = (float*)data;
        break;
    case Out2:
        self->out2 = (float*)data;
        break;
    case OutMono:
        self->outMono = (float*)data;
        break;
    case SummedGain:
        self->summedGain = (float*)data;
        break;
	}
}

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
    Plugin* self = (Plugin*)malloc(sizeof(Plugin));

    if (!self) {
        return NULL;
    }

    self->samplerate = rate;
    // Get host features
    for (int i = 0; features[i]; ++i) {
        if (!strcmp(features[i]->URI, LV2_URID__map)) {
            self->map = (LV2_URID_Map*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_WORKER__schedule)) {
            self->schedule = (LV2_Worker_Schedule*)features[i]->data;
        } else if (!strcmp(features[i]->URI, LV2_LOG__log)) {
            self->log = (LV2_Log_Log*)features[i]->data;
        }
    }
    if (!self->map) {
        lv2_log_error(&self->logger, "Missing feature urid:map\n");
        goto fail;
    } else if (!self->schedule) {
        lv2_log_error(&self->logger, "Missing feature work:schedule\n");
        goto fail;
    }

    // Map URIs and initialise forge/logger
    map_uris(self->map, &self->uris);
    lv2_atom_forge_init(&self->forge, self->map);
    lv2_log_logger_init(&self->logger, self->map, self->log);

    ReadHWJackValues(self);

    self->refresh_counter = self->samplerate * REFRESH_TIME_S;

	return (LV2_Handle)self;

fail:
    free(self);
    return 0;

}

static void
cleanup(LV2_Handle instance)
{
    Plugin* self = (Plugin*) instance;

	free(self);
}

static void
activate(LV2_Handle instance)
{
}


static void
run(LV2_Handle instance, uint32_t n_samples)
{
    Plugin* self = (Plugin*) instance;

    float summedGain = (float)*self->summedGain;

    for ( uint32_t i = 0; i < n_samples; i++)
    {
        if (self->stereo_jack_plugged){
            //copy in to outp
            self->out1[i] = self->in1[i];
            self->out2[i] = self->in2[i];

            //sum and possibly compensate for gain
            self->outMono[i] = (self->in1[i] + self->in2[i]) * summedGain;
        }
        else if (self->inp_jack_1_value) {
            //copy in to outp
            self->out1[i] = self->in1[i];
            self->out2[i] = self->in1[i];
            self->outMono[i] = self->in1[i];
        }
        else if (self->inp_jack_2_value) {
            //copy in to outp
            self->out1[i] = self->in2[i];
            self->out2[i] = self->in2[i];
            self->outMono[i] = self->in2[i];
        }
        else {
            //zero
            self->out1[i] = 0.f;
            self->out2[i] = 0.f;
            self->outMono[i] = 0.f;
        }

        if (self->refresh_counter > 0) {
            self->refresh_counter--;
        }
        else {
            self->refresh_counter = self->samplerate * REFRESH_TIME_S;

            // do not read files in realtime thread
            LV2_Atom atom = { 0 , self->uris.uri_loadHWJackValues };
            self->schedule->schedule_work(self->schedule->handle, sizeof(atom), &atom);
        }
    }

}

static void
deactivate(LV2_Handle instance)
{
}


static const void*
extension_data(const char* uri)
{
    static const LV2_Worker_Interface worker = { work, work_response, NULL };
    if (!strcmp(uri, LV2_WORKER__interface)) {
        return &worker;
    }
    return NULL;
}

static const LV2_Descriptor descriptor = {
	PLUGIN_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:  return &descriptor;
	default: return NULL;
	}
}
