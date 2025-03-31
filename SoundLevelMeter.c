#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/i2c.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// I2C defines
// This example will use I2C0 on GPIO8 (SDA) and GPIO9 (SCL) running at 400KHz.
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9
#define I2C_DEVICE_ADDRESS 0x48

#define FLASH_TARGET_OFFSET ((1024*1024*2)-(FLASH_SECTOR_SIZE*2))        // user flash region 2 x flash sectors before top of flash mem
const uint8_t *flash_target = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);


#define DEBUG

int quiet, normal, loud, tooloud;   // threshold variables

void trafficlights(int delay)
{
gpio_put(0,1);
sleep_ms(delay*1000);
gpio_put(0, 0);
gpio_put(1,1);
sleep_ms(delay*1000);
gpio_put(1, 0);
gpio_put(2,1);
sleep_ms(delay*1000);
gpio_put(2, 0);
gpio_put(3,1);
sleep_ms(delay*1000);
gpio_put(3, 0);


printf("Turning ON all lights\n");

gpio_put(0,1);
gpio_put(1,1);
gpio_put(2,1);
gpio_put(3,1);

sleep_ms(delay*1000);

printf("Turning OFF all lights\n");

gpio_put(0, 0);
gpio_put(1, 0);
gpio_put(2, 0);
gpio_put(3, 0);

sleep_ms(delay*1000);
}

void init_gpio(void)
{
gpio_init(0);
gpio_init(1);
gpio_init(2);
gpio_init(3);

gpio_set_dir(0, GPIO_OUT);
gpio_set_dir(1, GPIO_OUT);
gpio_set_dir(2, GPIO_OUT);
gpio_set_dir(3, GPIO_OUT);

gpio_put(0, 0);
gpio_put(1, 0);
gpio_put(2, 0);
gpio_put(3, 0);
}

void init_i2c(void)
{
// I2C Initialisation. Using it at 100Khz.
i2c_init(I2C_PORT, 100*1000);

gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
gpio_pull_up(I2C_SDA);
gpio_pull_up(I2C_SCL);

// Make the I2C pins available to picotool
bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));

// For more examples of I2C use see https://github.com/raspberrypi/pico-examples/tree/master/i2c
}

// I2C reserves some addresses for special purposes. We exclude these from the scan.
// These are any addresses of the form 000 0xxx or 111 1xxx
bool reserved_addr(uint8_t addr)
{
return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
}

int i2c_scan(void)
{
printf("\nI2C Bus Scan\n");
printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");

for (int addr = 0; addr < (1 << 7); ++addr)
{
    if (addr % 16 == 0)
    {
        printf("%02x ", addr);
    }

    // Perform a 1-byte dummy read from the probe address. If a slave
    // acknowledges this address, the function returns the number of bytes
    // transferred. If the address byte is ignored, the function returns
    // -1.

    // Skip over any reserved addresses.
    int ret;
    uint8_t rxdata;
    if (reserved_addr(addr))
        ret = PICO_ERROR_GENERIC;
    else
        ret = i2c_read_blocking(i2c_default, addr, &rxdata, 1, false);

    printf(ret < 0 ? "." : "@");
    printf(addr % 16 == 15 ? "\n" : "  ");
}
printf("Done.\n");
return 0;

}

void get_values(uint8_t regnum, uint8_t *rbuf, uint8_t num_bytes)
{
// send address of register to read from
i2c_write_blocking(I2C_PORT, I2C_DEVICE_ADDRESS, &regnum, 1, 1);

i2c_read_blocking(I2C_PORT, I2C_DEVICE_ADDRESS, rbuf, num_bytes, 0);
}

void set_values(uint8_t regnum, uint8_t *wbuf, uint8_t num_bytes)
{
uint8_t mybuf[33];
mybuf[0] = regnum;
for(uint8_t count=0; count<num_bytes;count++)
    mybuf[count+1] = wbuf[count];

//     i2c_write_blocking(I2C_PORT, I2C_DEVICE_ADDRESS, &regnum, 1, 1);
i2c_write_blocking(I2C_PORT, I2C_DEVICE_ADDRESS, mybuf, num_bytes+1, 0);
}


void flash_save(void)
{
    uint8_t ram_buffer[FLASH_PAGE_SIZE];              // RAM staging for flash write

    for(uint16_t count=0; count<FLASH_PAGE_SIZE; count++)
        ram_buffer[count] = flash_target[count];      // load buffer with Flash contents

    ram_buffer[0] = quiet;                            // load thresholds into RAM buffer
    ram_buffer[1] = normal;
    ram_buffer[2] = loud;
    ram_buffer[3] = tooloud;

    uint32_t ints = save_and_disable_interrupts();      // USB Uart uses interrupts, so disable
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*) ram_buffer, FLASH_PAGE_SIZE);
    restore_interrupts (ints);
}

void flash_load(void)
{
    if(flash_target[0] == 0xFF)                         // Flash has not been programmed
    {
        quiet = 50; normal = 70; loud = 77; tooloud = 84;   // set threshold initial levels
    }
    else
    {
        quiet = flash_target[0];                            // load variable from flash store
        normal = flash_target[1];                           // load variable from flash store
        loud = flash_target[2];                             // load variable from flash store
        tooloud = flash_target[3];                          // load variable from flash store
    }
}


/*
* Menu system for USB serial connection.
* Menu options: q n l t (quiet, normal, loud, tooloud)
* type the command letter (as above) and RETURN
* the menu then prompts for new value for that sound level
*
* this function is entered when user types something followed by RETURN
* the (first) (first) (first) (first) (first) (first) (first) (first) (first) typed letter is passed to this function
*/
void menu(char input)
{
    char buf[10];        // buffer for reading input
    int readval;      // char read from input

    printf("\n");       // follow input char with RETURN

    switch(input)
    {
        case 'q':       // set quiet level
            printf("Quiet dB level = %d New level: ", quiet);
            readval = atoi(gets(buf));          // read from USB serial and convert to int
            if(readval > 0)     // valid number entered
            {
                quiet = readval;    // set new value
                printf("\nQuiet level set to %d\n", quiet);
            }
            else
            {
                printf("\nEntry must be valid number greater than 0\n");
            }
            break;

        case 'n':
            printf("Normal dB level = %d New level: ", normal);
            readval = atoi(gets(buf));
            if(readval > quiet)     // number must be greater than lower thresholds
            {
                normal = readval;    // set new value
                printf("\nNormal level set to %d\n", normal);
            }
            else
            {
                printf("\nNumber must be greater than lower thresholds.\n");
            }
            break;

        case 'l':
            printf("Loud dB level = %d New level: ", loud);
            readval = atoi(gets(buf));
            if(readval > normal)     // number must be greater than lower thresholds
            {
                loud = readval;    // set new value
                printf("\nLoud level set to %d\n", loud);
            }
            else
            {
                printf("\nNumber must be greater than lower thresholds.\n");
            }
            break;

        case 't':
            printf("TooLoud dB level = %d New level: ", tooloud);
            readval = atoi(gets(buf));
            if(readval > loud)     // number must be greater than lower thresholds
            {
                tooloud = readval;    // set new value
                printf("\nTooLoud level set to %d\n", tooloud);
            }
            else
            {
                printf("\nNumber must be greater than lower thresholds.\n");
            }
            break;

        case 'r':
            quiet = 50; normal = 70; loud = 77; tooloud = 84;   // set threshold initial levels
            break;
    
        case 's':                                               // save threshold values to FLASH
            flash_save();
            printf("Saved threshold values to FLASH.\n");
            break;
        
        default:
            printf("\nType letter for threshold to set:\nq - Quiet\nn - Normal\nl - Loud\nt - TooLoud\nr - reset to defaults\n");
    }
    if(quiet >= normal)
        normal = quiet + 2;     // normalise values (ensure each level is higher than the last)
    if (normal >= loud)
        loud = normal + 2;
    if(loud >= tooloud)
        tooloud = loud + 2;

    printf("\nCurrent threshold settings:\nQuiet:   %d\nNormal:  %d\nLoud:    %d\nTooLoud: %d\n\n", quiet, normal, loud, tooloud);
    sleep_ms(3000);     // allow time for user to read above message
}


int main()
{
    char rbuf[32];     // buffer for reading from i2c device
    char wbuf[32];     // buffer for writing to i2c device

    stdio_init_all();

    init_i2c();
    init_gpio();

    flash_load();                      // get levels from FLASH (or default)
#ifdef NEVER
    sleep_ms(5000);
    printf("Hello, world!\n");

    get_values(0x07, rbuf, 2);          // read two bytes starting at reg 7
    printf("Initial avg value = %x %x\n", rbuf[0], rbuf[1]);

    wbuf[0] = 0x07; wbuf[1] = 0xD0;     // set avg value to dec. 2000 (hex 07D0)
    set_values(0x07, wbuf, 2);
    sleep_ms(100);

    get_values(0x07, rbuf, 2);          // read two bytes starting at reg 7
    printf("New avg value = %x %x\n", rbuf[0], rbuf[1]);

    sleep_ms(5000);
#endif // NEVER

    while (true)
    {
        char inchar;                 // char read from stdin

        // sleep_ms(1000);

        get_values(0x0A, rbuf, 1);
        printf("%d dB", rbuf[0]);
    
        gpio_put(0, 0);                 // turn off all LEDs initially
        gpio_put(1, 0);
        gpio_put(2, 0);
        gpio_put(3, 0);


        if(rbuf[0] > quiet)                // very quiet
        {
            gpio_put(0,1);              // turn on first (green) LED
            printf(" GREEN");
        }

        if(rbuf[0] > normal)                // normal
        {
            gpio_put(1,1);              // turn on second (yellow) LED
            printf(" YELLOW");
        }

        if(rbuf[0] > loud)                // loud
        {
            gpio_put(2,1);              // turn on third (orange) LED
            printf(" ORANGE");
        }

        if(rbuf[0] > tooloud)                // TOO LOUD
        {
            gpio_put(3,1);              // turn on fourth (red) LED
            printf(" RED!");
        }
        printf("\n");
        
        inchar = getchar_timeout_us(1000000);     // read character from stdin (usb)
        if(inchar != 254)               // a character has been input
            menu(inchar);               // go to menu system
    }
}
