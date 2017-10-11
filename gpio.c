/**
 * Various tests of fast GPIO on the Raspberry Pi
 *
 * Compile with gcc -O2 -o gpio
 * sudo bash
 * Run with ./gpio
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

// Access from ARM Running Linux
// 0x20000000 for Rpi Zero
#define BCM2708_PERI_BASE ( 0x20000000 )
// GPIO controller
#define GPIO_BASE         ( BCM2708_PERI_BASE + 0x200000 ) 
// 1 MHz timer
#define TIMER_BASE        ( BCM2708_PERI_BASE + 0x00003000 )
#define TIMER_OFFSET        ( 4 )
#define INT_BASE ( BCM2708_PERI_BASE + 0x0000B000 )

#define PAGE_SIZE  ( 4 * 1024 )
#define BLOCK_SIZE ( 4 * 1024 )

int  mem_fd;

void *gpio_map;
void *timer_map;
void *int_map;


// I/O access
volatile unsigned *gpio;
// Timer access
volatile unsigned *timer;
volatile unsigned *intrupt;

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or SET_GPIO_ALT(x,y)
#define INP_GPIO(g) *(gpio+((g)/10)) &= ~(7<<(((g)%10)*3))
#define OUT_GPIO(g) *(gpio+((g)/10)) |=  (1<<(((g)%10)*3))
#define SET_GPIO_ALT(g,a) *(gpio+(((g)/10))) |= (((a)<=3?(a)+4:(a)==4?3:2)<<(((g)%10)*3))

#define GPIO_SET *(gpio+7)  // sets   bits which are 1 ignores bits which are 0
#define GPIO_CLR *(gpio+10) // clears bits which are 1 ignores bits which are 0

#define GET_GPIO(g) (*(gpio+13)&(1<<g)) // 0 if LOW, (1<<g) if HIGH

#define GPIO_PULL *(gpio+37) // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio+38) // Pull up/pull down clock

#define NUMBER_OF_PULSES ( 100000000 )





/******************** INTERRUPTS *************

Is this safe?
Dunno, but it works

interrupts(0)   disable interrupts
interrupts(1)   re-enable interrupts

return 1 = OK
       0 = error with message print

Uses intrupt pointer set by setup()
Does not disable FIQ which seems to
cause a system crash
Avoid calling immediately after keyboard input
or key strokes will not be dealt with properly

*******************************************/

int interrupts(int flag)
  {
  static unsigned int sav132 = 0;
  static unsigned int sav133 = 0;
  static unsigned int sav134 = 0;

  if(flag == 0)    // disable
    {
    if(sav132 != 0)
      {
      // Interrupts already disabled so avoid printf
      return(0);
      }

    if( (*(intrupt+128) | *(intrupt+129) | *(intrupt+130)) != 0)
      {
      printf("Pending interrupts\n");  // may be OK but probably
      return(0);                       // better to wait for the
      }                                // pending interrupts to
                                       // clear

    sav134 = *(intrupt+134);
    *(intrupt+137) = sav134;
    sav132 = *(intrupt+132);  // save current interrupts
    *(intrupt+135) = sav132;  // disable active interrupts
    sav133 = *(intrupt+133);
    *(intrupt+136) = sav133;
    }
  else            // flag = 1 enable
    {
    if(sav132 == 0)
      {
      printf("Interrupts not disabled\n");
      return(0);
      }

    *(intrupt+132) = sav132;    // restore saved interrupts
    *(intrupt+133) = sav133;
    *(intrupt+134) = sav134;
    sav132 = 0;                 // indicates interrupts enabled
    }
  return(1);
  }

  
//
// Set up a memory regions to access GPIO
//
void setup_mmap()
{
   // if ((mem_fd = open("/dev/mem", O_RDONLY ) ) < 0) {
      // printf("can't open /dev/mem for timer\n");
      // exit(-1);
   // }
   
   /* open /dev/mem */
   if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC) ) < 0) {
      printf("can't open /dev/mem for gpio\n");
      exit(-1);
   }


    // MMAP timer
   timer_map = mmap(
    NULL, 
    BLOCK_SIZE,
    PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
    MAP_SHARED, 
    mem_fd, 
    TIMER_BASE );

   // close(mem_fd); //No need to keep mem_fd open after mmap

   /* mmap GPIO */
   gpio_map = mmap(
      NULL,             //Any adddress in our space will do
      BLOCK_SIZE,       //Map length
      PROT_READ|PROT_WRITE,// Enable reading & writting to mapped memory
      MAP_SHARED,       //Shared with other processes
      mem_fd,           //File to map
      GPIO_BASE         //Offset to GPIO peripheral
   );
   
   // interrupts
  int_map = mmap(NULL,4096,PROT_READ|PROT_WRITE,
                  MAP_SHARED,mem_fd,INT_BASE);

   
   close(mem_fd); //No need to keep mem_fd open after mmap

   if (timer_map == MAP_FAILED) {
      printf("timer mmap error %d\n", (int)timer_map);//errno also set!
      exit(-1);
   }

   if (gpio_map == MAP_FAILED) {
      printf("gpio mmap error %d\n", (int)gpio_map);//errno also set!
      exit(-1);
   }

   if (int_map == MAP_FAILED) {
      printf("int mmap error %d\n", (int)int_map);//errno also set!
      exit(-1);
   }

   // Always use volatile pointer!
   gpio = (volatile unsigned *)gpio_map;
   timer = (volatile unsigned *)timer_map;
   intrupt = (volatile unsigned *)int_map;
} // setup_io


void printButton(int g)
{
  if (GET_GPIO(g)) // !=0 <-> bit is 1 <- port is HIGH=3.3V
    printf("Button pressed!\n");
  else // port is LOW=0V
    printf("Button released!\n");
}


int main(int argc, char **argv)
{
  int g,rep;
  uint32_t counter = 0;
  uint32_t buffer[ 1024*1024 ]; // 4 MB
  uint32_t *pTimerValueL, *pTimerValueH, timerStartL, timerStartH, timerEndL, timerEndH, delayTimer, delayTimerPrev, maxDelay = 0, tmp;

  printf("Starting GPIO & timer test for %d pulses\n", NUMBER_OF_PULSES );

  // Set up gpi and timer pointer for direct register access
  setup_mmap();
  pTimerValueL = (uint32_t *)((char *)timer + 4 );
  pTimerValueH = (uint32_t *)((char *)timer + 8 );
  
  interrupts( 0 );
  
  timerStartL = *pTimerValueL;
  timerStartH = *pTimerValueH;

  // Set GPIO pin 4 to output
  INP_GPIO(4); // must use INP_GPIO before we can use OUT_GPIO
  OUT_GPIO(4);
  
  delayTimerPrev = *pTimerValueL;
  
  while( counter < NUMBER_OF_PULSES ) {
    GPIO_SET = 1<<4;
    GPIO_CLR = 1<<4;
    counter++;
    buffer[ counter & 0xFFFFF ] = counter;
    delayTimer = *pTimerValueL;
    // TODO Rollover check...
    tmp = delayTimer - delayTimerPrev;
    if ( tmp > maxDelay ) maxDelay = tmp;
    delayTimerPrev = delayTimer;
  }
  timerEndL = *pTimerValueL;
  timerEndH = *pTimerValueH;
  
  interrupts( 1 );

  
  printf("Done!\nCounter = %d, buffer[1234] = %d\n", counter, buffer[ 1234 ] );
  printf("Time: %d us\n", timerEndL - timerStartL );
  printf("Frequency: %6.3f Mhz\n", 1.0 * NUMBER_OF_PULSES / ( timerEndL - timerStartL ) );
  printf("Max delay: %d us\n", maxDelay );

  return 0;

} // main


