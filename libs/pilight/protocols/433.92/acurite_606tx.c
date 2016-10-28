#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ACURITE_HAS_TEMPERATURE
//#define ACURITE_HAS_HUMIDITY
#define ACURITE_HAS_BATTERY
#define ACURITE_DEBUG

#include "../../core/pilight.h"
#include "../../core/common.h"
#include "../../core/dso.h"
#include "../../core/log.h"
#include "../protocol.h"
#include "../../core/binary.h"
#include "../../core/gc.h"
#include "acurite_606tx.h"

#define PULSE_MULTIPLIER    4
#define MIN_PULSE_LENGTH    260
#define AVG_PULSE_LENGTH    268
#define MAX_PULSE_LENGTH    270
#define RAW_LENGTH          66

#define BIT1_LENGTH 4000
#define BIT0_LENGTH 2000

typedef struct settings_t {
    double id;
#ifdef ACURITE_HAS_TEMPERATURE
    double temp;
    int isCelsius;
#endif
#ifdef ACURITE_HAS_HUMIDITY
    double humi;
#endif
    struct settings_t *next;
} settings_t;

static struct settings_t *settings = NULL;

static int validate(void) {
    if(acurite_606tx->rawlen == RAW_LENGTH) {
#ifdef ACURITE_DEBUG
        logprintf(LOG_DEBUG, "acurite_606tx last index [%d] = %d", acurite_606tx->rawlen-1, acurite_606tx->raw[acurite_606tx->rawlen-1]);
#endif
        if(acurite_606tx->raw[acurite_606tx->rawlen-1] >= (MIN_PULSE_LENGTH*PULSE_DIV) &&
           acurite_606tx->raw[acurite_606tx->rawlen-1] <= (MAX_PULSE_LENGTH*PULSE_DIV)) {
            return 0;
        }
    }
    
    return -1;
}

static void parseCode(void) {
    int i = 0, x = 0, binary[RAW_LENGTH/2];
#ifdef ACURITE_DEBUG
    char binOut[RAW_LENGTH/2];
#endif
    int check = 0;

#ifdef ACURITE_HAS_TEMPERATURE
    double temp_offset = 0.0;
    double temperature = 0.0;
    int isCelsius = 1;
#endif
#ifdef ACURITE_HAS_HUMIDITY
    double humidity = 0.0;
    double humi_offset = 0.0;
#endif
    int id = 0;
#ifdef ACURITE_HAS_BATTERY
    int battery = 0;
#endif
    
    for(x=1;x<acurite_606tx->rawlen-1;x+=2) {
        check = (int)((double)AVG_PULSE_LENGTH*(PULSE_MULTIPLIER*2));
        
        if (acurite_606tx->raw[x] > check) {
#ifdef ACURITE_DEBUG
            binOut[i] = '1';
#endif
            binary[i++] = 1;
        } else {
#ifdef ACURITE_DEBUG
            binOut[i] = '0';
#endif
            binary[i++] = 0;
        }
    }
    
#ifdef ACURITE_DEBUG
    logprintf(LOG_DEBUG, "acurite_606tx code %s", binOut);
#endif
    
    id = binToDecRev(binary, 0, 7);
#ifdef ACURITE_HAS_BATTERY
    battery = binary[8];
#endif
#ifdef ACURITE_HAS_TEMPERATURE
    temperature = ((double)binToDecRev(binary, 12, 23));
    
    // if over 70.0C, flip signs
    temperature /= 10;
    if(temperature > 70) {
        temperature -= 110;
    }
#endif
#ifdef ACURITE_HAS_HUMIDITY
    humidity = (double)binToDec(binary, 28, 35);
#endif
    
    struct settings_t *tmp = settings;
    while(tmp) {
        if(fabs(tmp->id-id) < EPSILON) {
#ifdef ACURITE_HAS_HUMIDITY
            humi_offset = tmp->humi;
#endif
#ifdef ACURITE_HAS_TEMPERATURE
            temp_offset = tmp->temp;
            isCelsius = tmp->isCelsius;
#endif
            break;
        }
        tmp = tmp->next;
    }
    
#ifdef ACURITE_HAS_TEMPERATURE
    temperature += temp_offset;
    if (isCelsius == 0) {
        //temperature = temperature * 1.8 + 32;
        temperature = C2F(temperature);
    }
#endif
#ifdef ACURITE_HAS_HUMIDITY
    humidity += humi_offset;
#endif

    acurite_606tx->message = json_mkobject();
    json_append_member(acurite_606tx->message, "id", json_mknumber(id, 0));
#ifdef ACURITE_HAS_TEMPERATURE
    json_append_member(acurite_606tx->message, "temperature", json_mknumber(temperature, 1));
    json_append_member(acurite_606tx->message, "is-celsius", json_mknumber(isCelsius, 0));
#endif
#ifdef ACURITE_HAS_HUMIDITY
    json_append_member(acurite_606tx->message, "humidity", json_mknumber(humidity, 1));
#endif
#ifdef ACURITE_HAS_BATTERY
    json_append_member(acurite_606tx->message, "battery", json_mknumber(battery, 0));
#endif
}

static int checkValues(struct JsonNode *jvalues) {
    struct JsonNode *jid = NULL;

    if((jid = json_find_member(jvalues, "id"))) {
        struct settings_t *snode = NULL;
        struct JsonNode *jchild = NULL;
        struct JsonNode *jchild1 = NULL;
        double id = -1;
        int match = 0;

        jchild = json_first_child(jid);
        while(jchild) {
            jchild1 = json_first_child(jchild);
            while(jchild1) {
                if(strcmp(jchild1->key, "id") == 0) {
                    id = jchild1->number_;
                }
                jchild1 = jchild1->next;
            }
            jchild = jchild->next;
        }

        struct settings_t *tmp = settings;
        while(tmp) {
            if(fabs(tmp->id-id) < EPSILON) {
                match = 1;
                break;
            }
            tmp = tmp->next;
        }

        if(match == 0) {
            if((snode = MALLOC(sizeof(struct settings_t))) == NULL) {
                fprintf(stderr, "out of memory\n");
                exit(EXIT_FAILURE);
            }
            snode->id = id;
            
#ifdef ACURITE_HAS_TEMPERATURE
            snode->temp = 0;
            json_find_number(jvalues, "temperature-offset", &snode->temp);
            json_find_number(jvalues, "is-celsius", &snode->isCelsius);
#endif

#ifdef ACURITE_HAS_HUMIDITY
            snode->humi = 0;
            json_find_number(jvalues, "humidity-offset", &snode->humi);
#endif

            snode->next = settings;
            settings = snode;
        }
    }
    return 0;
}

static void gc(void) {
    struct settings_t *tmp = NULL;
    while(settings) {
        tmp = settings;
        settings = settings->next;
        FREE(tmp);
    }
    if(settings != NULL) {
        FREE(settings);
    }
}

#if !defined(MODULE) && !defined(_WIN32)
__attribute__((weak))
#endif
void acurite606TXInit(void) {

    protocol_register(&acurite_606tx);
    protocol_set_id(acurite_606tx, "acurite_606tx");
    protocol_device_add(acurite_606tx, "acurite_606tx", "Acu-Rite 00606TX Weather Stations");
    acurite_606tx->devtype = WEATHER;
    acurite_606tx->hwtype = RF433;
    acurite_606tx->minrawlen = RAW_LENGTH;
    acurite_606tx->maxrawlen = RAW_LENGTH;

    options_add(&acurite_606tx->options, 'i', "id", OPTION_HAS_VALUE, DEVICES_ID, JSON_NUMBER, NULL, "[0-9]");
    
    // options_add(&acurite_606tx->options, 0, "decimals", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)1, "[0-9]");
    
#ifdef ACURITE_HAS_TEMPERATURE
    options_add(&acurite_606tx->options, 't', "temperature", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[0-9]{1,3}$");
    options_add(&acurite_606tx->options, 0, "temperature-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
    options_add(&acurite_606tx->options, 0, "temperature-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "[0-9]");
    options_add(&acurite_606tx->options, 0, "is-celsius", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, (void *)1, "[0-9]");
    options_add(&acurite_606tx->options, 0, "show-temperature", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
#endif
    
#ifdef ACURITE_HAS_HUMIDITY
    options_add(&acurite_606tx->options, 'h', "humidity", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "[0-9]");
    options_add(&acurite_606tx->options, 0, "humidity-offset", OPTION_HAS_VALUE, DEVICES_SETTING, JSON_NUMBER, (void *)0, "[0-9]");
    options_add(&acurite_606tx->options, 0, "humidity-decimals", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "[0-9]");
    options_add(&acurite_606tx->options, 0, "show-humidity", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
#endif

#ifdef ACURITE_HAS_BATTERY
    options_add(&acurite_606tx->options, 'b', "battery", OPTION_HAS_VALUE, DEVICES_VALUE, JSON_NUMBER, NULL, "^[01]$");
    options_add(&acurite_606tx->options, 0, "show-battery", OPTION_HAS_VALUE, GUI_SETTING, JSON_NUMBER, (void *)1, "^[10]{1}$");
#endif

    acurite_606tx->parseCode=&parseCode;
    acurite_606tx->checkValues=&checkValues;
    acurite_606tx->validate=&validate;
    acurite_606tx->gc=&gc;
}

#if defined(MODULE) && !defined(_WIN32)
void compatibility(struct module_t *module) {
    module->name = "acurite_606tx";
    module->version = "1.0";
    module->reqversion = "6.0";
    module->reqcommit = "1";
}

void init(void) {
    acurite606TXInit();
}
#endif
