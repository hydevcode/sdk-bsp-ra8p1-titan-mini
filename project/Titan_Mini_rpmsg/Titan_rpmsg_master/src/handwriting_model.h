#ifndef HANDWRITING_MODEL_H__
#define HANDWRITING_MODEL_H__

#include <stdint.h>

#define HANDWRITING_MODEL_INPUT_SIZE   (28U * 28U)
#define HANDWRITING_MODEL_OUTPUT_SIZE  (10U)

int handwriting_model_init(void);
int handwriting_model_run(int8_t const *input_28x28, int8_t *scores, uint8_t *digit);
int handwriting_model_process_rgb565(uint16_t *frame, int width, int height, uint16_t rotation);

#endif
