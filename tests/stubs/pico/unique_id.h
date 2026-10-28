#include <stddef.h>
#include <stdint.h>
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8
typedef struct { uint8_t id[8]; } pico_unique_board_id_t;
void pico_get_unique_board_id(pico_unique_board_id_t *id);
void pico_get_unique_board_id_string(char *id_out, unsigned len);
