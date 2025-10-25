/*
 * This program converts a color PPM image (P3 format) to grayscale.
 * It is designed to be robust, handling comments and varied whitespace in the PPM header. 
 * It uses buffered I/O and a lookup table for efficient processing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define INPUT_FILE "im.ppm"
#define OUTPUT_FILE "im-gray.ppm"
#define BUFFER_SIZE (256 * 1024)
#define MAX_DIMENSION 100000

/* Skips comment lines in a PPM file. A comment line starts with '#' */
static int skip_comments(FILE *f, int c) {
    while (c == '#') {
        /* Skip to end of line */
        do {
            c = getc(f);
            if (c == EOF) return EOF;
        } while (c != '\n');
        c = getc(f);  /* Read first char after newline */
    }
    return c;
}

/*
 * Universal integer reading function.
 * Skips whitespace and comments, reads the next unsigned integer.
 * Used for both header parsing and pixel values.
 */
static int read_uint(FILE *f, int *out, int max_allowed) {
    int c;
    int val = 0;
    int found_digit = 0;

    while (1) {
        c = getc(f);
        if (c == EOF) return 0;

        if (c == '#') {
            /* Found comment, skip to end of line */
            while ((c = getc(f)) != '\n' && c != EOF);
        } else if (isspace(c)) {
            /* Continue skipping whitespace */
            continue;
        } else {
            /* Found non-whitespace, non-comment character - ready to parse */
            break;
        }
    }

    if (!isdigit(c)) {
        /* First valid character is not a digit - invalid format */
        ungetc(c, f);
        return 0;
    }

    /* Parse the integer */
    do {
        found_digit = 1;
        int digit = c - '0';

        /* Check for overflow before multiplying */
        if (val > (max_allowed / 10) + 1) {
            val = max_allowed + 1; /* Mark as overflow */
        } else {
            val = val * 10 + digit;
        }

        c = getc(f);
    } while (isdigit(c));

    /* Put back the non-digit character to avoid consuming delimiters */
    if (c != EOF) {
        ungetc(c, f);
    }

    if (!found_digit || val > max_allowed) {
        return 0;
    }

    *out = val;
    return 1;
}

int main(void) {
    FILE *input_file = NULL, *output_file = NULL;
    char magic[3], *in_buf = NULL, *out_buf = NULL, *row_buf = NULL;
    int width, height, max_val;
    int ret = 1;

    /* LUT: textual representations of 0..255 and their byte lengths. */
    char num_text[256][4];    /* "0".."255" + NUL; copied via memcpy without NUL */
    uint8_t num_len[256];

    /* Open input file in binary mode. */
    if ((input_file = fopen(INPUT_FILE, "rb")) == NULL) {
        fprintf(stderr, "Error: Cannot open input file '%s'\n", INPUT_FILE);
        return 1;
    }
    /* Attach input buffer for efficient reading */
    if ((in_buf = malloc(BUFFER_SIZE)) != NULL) {
        setvbuf(input_file, in_buf, _IOFBF, BUFFER_SIZE);
    }

    /* Parse and validate header - skip any comments.
     * Using isspace() for consistent whitespace handling across all parsing. */
    int c;
    do {
        c = getc(input_file);
        if (c == EOF) {
            fprintf(stderr, "Error: Unexpected EOF in header\n");
            goto cleanup;
        }
        c = skip_comments(input_file, c);
    } while (isspace(c));
    ungetc(c, input_file);
    
    if (fscanf(input_file, "%2s", magic) != 1 || strcmp(magic, "P3") != 0) {
        fprintf(stderr, "Error: Unsupported PPM magic number (expected P3)\n");
        goto cleanup;
    }
    
    /* Read width and height with comment support.
     * Replaced fscanf with read_uint to properly handle inline comments
     * like "2000 # width". fscanf fails on such inputs because it doesn't skip '#'. */
    if (!read_uint(input_file, &width, MAX_DIMENSION) ||
        !read_uint(input_file, &height, MAX_DIMENSION)) {
        fprintf(stderr, "Error: Failed to read image dimensions\n");
        goto cleanup;
    }
    if (width <= 0 || height <= 0 || width > MAX_DIMENSION || height > MAX_DIMENSION) {
        fprintf(stderr, "Error: Invalid image dimensions (%dx%d), must be 1-%d\n", 
                width, height, MAX_DIMENSION);
        goto cleanup;
    }
    
    /* Check for potential overflow in pixel count */
    if ((long long)width * height > (long long)MAX_DIMENSION * MAX_DIMENSION / 10) {
        fprintf(stderr, "Error: Image too large (%dx%d pixels)\n", width, height);
        goto cleanup;
    }
    
    /* Read max value with comment support */
    if (!read_uint(input_file, &max_val, 65535)) {
        fprintf(stderr, "Error: Failed to read maximum color value\n");
        goto cleanup;
    }
    if (max_val != 255) {
        /*
         * This implementation only supports a maximum color value of 255.
         */
        fprintf(stderr, "Error: Maximum color value must be 255 (got %d)\n", max_val);
        goto cleanup;
    }

    if ((output_file = fopen(OUTPUT_FILE, "wb")) == NULL) {
        fprintf(stderr, "Error: Cannot open output file '%s'\n", OUTPUT_FILE);
        goto cleanup;
    }
    /* Attach output buffer for efficient writing */
    if ((out_buf = malloc(BUFFER_SIZE)) != NULL) {
        setvbuf(output_file, out_buf, _IOFBF, BUFFER_SIZE);
    }

    if (fprintf(output_file, "P3\n%d %d\n%d\n", width, height, max_val) < 0) {
        fprintf(stderr, "Error: Failed to write output header\n");
        goto cleanup;
    }

    /*
     * Pre-generate string representations for numbers 0-255 to avoid repeated snprintf calls in the main loop, which improves performance
     */
    for (int v = 0; v <= 255; v++) {
        int n = snprintf(num_text[v], sizeof num_text[v], "%d", v);
        if (n <= 0) n = 1; /* should not happen */
        num_len[v] = (uint8_t)n;
    }

    /* Allocate row buffer for one-write-per-row output */
    size_t row_cap = (size_t)width * (3 * 3 + 3) + 2;  /* Conservative estimate */
    if ((row_buf = malloc(row_cap)) == NULL) {
        fprintf(stderr, "Error: Cannot allocate row buffer (%zu bytes)\n", row_cap);
        goto cleanup;
    }

    /* Main loop: build each row and write once */
    for (int y = 0; y < height; y++) {
        size_t pos = 0;  /* Current position in row buffer */
        
        for (int x = 0; x < width; x++) {
            /* Read RGB pixel values */
            int r, g, b;
            if (!read_uint(input_file, &r, 255) || 
                !read_uint(input_file, &g, 255) || 
                !read_uint(input_file, &b, 255)) {
                fprintf(stderr, "Error: Failed to read pixel data at row %d, col %d\n", y, x);
                goto cleanup;
            }
            
            /* Validate pixel values are in valid range */
            if ((unsigned)r > 255u || (unsigned)g > 255u || (unsigned)b > 255u) {
                fprintf(stderr, "Error: Pixel value out of range at row %d, col %d\n", y, x);
                goto cleanup;
            }
            
            /* Calculate grayscale value (simple average) */
            int gray = (r + g + b) / 3;

            /* Append grayscale triplet to the row buffer */
            memcpy(row_buf + pos, num_text[gray], num_len[gray]);
            pos += num_len[gray];
            row_buf[pos++] = ' ';
            
            memcpy(row_buf + pos, num_text[gray], num_len[gray]);
            pos += num_len[gray];
            row_buf[pos++] = ' ';
            
            memcpy(row_buf + pos, num_text[gray], num_len[gray]);
            pos += num_len[gray];
            
            /* Add space between pixels (except last pixel in row) */
            if (x != width - 1) {
                row_buf[pos++] = ' ';
            }
        }
        
        /* Add newline at end of row */
        row_buf[pos++] = '\n';
        
        /*Write the row.*/
        if (fwrite(row_buf, 1, pos, output_file) != pos) {
            fprintf(stderr, "Error: Write failure at row %d\n", y);
            goto cleanup;
        }
    }
    
    ret = 0;

cleanup:
    /* Clean up resources */
    if (input_file && fclose(input_file) != 0 && ret == 0) {
        fprintf(stderr, "Warning: Error closing input file\n");
        ret = 1;
    }
    if (output_file) {
        if (fclose(output_file) != 0 && ret == 0) {
            fprintf(stderr, "Error: Failed to close output file properly\n");
            ret = 1;
        }
        if (ret != 0) {
            remove(OUTPUT_FILE);  /* Clean up partial output on error */
        }
    }

    free(in_buf);
    free(out_buf);
    free(row_buf);
    return ret;
}

