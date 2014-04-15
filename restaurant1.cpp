/* Get restaurant information from SD Card */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <SD.h>

#include "lcd_image.h"

// standard U of A library settings, assuming Atmel Mega SPI pins
#define SD_CS    5  // Chip select line for SD card
#define TFT_CS   6  // Chip select line for TFT display
#define TFT_DC   7  // Data/command line for TFT
#define TFT_RST  8  // Reset line for TFT (or connect to +5V)

// Joystick
#define JOY_SEL 9 // Joystick SELECT
#define JOY_HORIZ 0 // Joystick HORIZONTAL movement
#define JOY_VERT 1 // Joystick VERTICAL movement
#define OFFSET 30 // offset required for "movement" to be registered
#define INCREMENT 2 // distance moved

// Rating Selector
#define RATING_DIAL 2 // Aanlog input A2 - restaurant dial selector
#define ANALOG_MAX 1000 // A bit less than 1023 to give some wiggle room on the top end of the scale

#define RATING_LED_0 2
#define RATING_LED_1 3
#define RATING_LED_2 4
#define RATING_LED_3 10
#define RATING_LED_4 11

// Color
#define COLOR_BG ST7735_WHITE

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Sd2Card card;

// Cursor
typedef struct {
  int16_t x;
  int16_t y;

  int16_t x_old;
  int16_t y_old;

  int16_t x_global;
  int16_t y_global;

  int16_t width;
  int16_t height;

  int16_t map_x;
  int16_t map_y;
} Cursor;

// Restaurant
#define NUM_RESTAURANTS 1066
#define RESTAURANT_START_BLOCK 4000000
#define RESTAURANTS_TO_DISPLAY 20
#define CHARS_PER_LINE 20

typedef struct {
  int32_t latitude;
  int32_t longitude;
  int8_t rating;
  char name[55];
} Restaurant;

typedef struct {
  uint16_t index;
  uint16_t dist;
  uint8_t rating;
} RestDist;

Restaurant restBuffer[8];
int lastRestBlock = -1;

void get_restaurant_fast(int i, Restaurant *r);
int32_t x_to_lon(int16_t x);
int32_t y_to_lat(int16_t y);
int16_t lon_to_x(int32_t lon);
int16_t lat_to_y(int32_t lat);

void selection_sort(RestDist *restDists);
void swap_restDist(RestDist *restDists, int i, int j);
void print_restaurants(RestDist*, int, int, int);

void setup() {
  tft.initR(INITR_BLACKTAB);
  tft.fillScreen(COLOR_BG);
  Serial.begin(9600);
  // Init Joystick Select
  pinMode(JOY_SEL, INPUT);
  digitalWrite(JOY_SEL, HIGH);
  // Init Rating Selector
  pinMode(RATING_DIAL, INPUT);
  int rating_leds[5] = {RATING_LED_0, RATING_LED_1, RATING_LED_2, RATING_LED_3, RATING_LED_4};
  for (int i=0; i < 5; i++) {
    pinMode(rating_leds[i], OUTPUT);
  }

  // Initialize SD Card
  Serial.print("Initializing SD Card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("failed");
    return;
  }
  Serial.println("OK!");

  Serial.print("Raw SD Initialization...");
  if (!card.init(SPI_HALF_SPEED, SD_CS)) {
    Serial.println("failed");
    return;
  }
  Serial.println("OK!");

  /* Draw Map */
  lcd_image_t map_image = {"yeg-big.lcd", 2048, 2048};
  lcd_image_draw(&map_image, &tft,
		 1024, 1024,      //the middle top-left corner of IMAGE
		 0, 0,      // upper-left corner of SCREEN
		 128, 160); // size of patch drawn

  // Init Cursor
  Cursor cursor = {64, 64, 64, 64, 1088, 1088, 3, 3, 1024, 1024}; // x, y, x_old, y_old, x_global, y_global, width, height, map x corner, map y corner

  int16_t joy_baseHoriz = analogRead(JOY_HORIZ);
  int16_t joy_baseVert = analogRead(JOY_VERT);
  bool pressAllowed = true;

  bool mapMode = true;
  bool sorted = false;

  bool looping = true;
  while (looping) {
    /* Handle Rating Selector Input */
    uint8_t min_rating = map(analogRead(RATING_DIAL), 0, ANALOG_MAX, 0, 5);
    for (int i=0; i < 5; i++) {
      if (min_rating > i) {
	digitalWrite(rating_leds[i], HIGH);
      } else {
	digitalWrite(rating_leds[i], LOW);
      }
    }

    /* Handle Joystick Click Input */
    if (digitalRead(JOY_SEL) == LOW && pressAllowed) {
      // swap modes
      mapMode = (mapMode ? false : true);
      Serial.println(mapMode ? "Map Mode" : "List Mode");
      if (mapMode) {
	// Redraw Map
	tft.fillScreen(COLOR_BG);
	lcd_image_draw(&map_image, &tft, cursor.map_x, cursor.map_y,
        0, 0, 128, 160);
      }
      pressAllowed = false;
      delay(50);//ms, for button bounce
    } else if (digitalRead(JOY_SEL) == HIGH) {
      pressAllowed = true;
    }
    




    /* Display Map Mode */
    if (mapMode) {
      /* We have entered map mode, therefore a new list will have 
	 to be sorted once we enter list mode again */
      sorted = false;

      // handle cursor relative movement
      int8_t diffx = 0;
      int8_t diffy = 0;
      diffx = cursor.x-cursor.x_old;
      diffy = cursor.y-cursor.y_old;
      cursor.x_global += diffx;
      cursor.y_global += diffy;
      cursor.x_old = cursor.x;
      cursor.y_old = cursor.y;
	Serial.println(cursor.y_global);

      if (analogRead(JOY_HORIZ) - joy_baseHoriz > OFFSET) {
	// move right
	cursor.x += INCREMENT;
	//at right edge of image
	if (cursor.x_global >= 2046){
	  cursor.x_global = 2046;
	  cursor.x = 126;
	  cursor.x_old = 126;
	}
	//near right edge of image
	if (cursor.x_global == 1920){
	  lcd_image_draw(&map_image, &tft, 1920, cursor.map_y, 
	  0, 0, 128, 160);
	  cursor.map_x = 1920;
	  cursor.x = 2;
	  cursor.x_old = cursor.x;
	  cursor.x_global = 1922;
	}
	// hit right edge of screen
	if (cursor.x >= tft.width()+1){
            lcd_image_draw(&map_image, &tft, cursor.map_x+128, cursor.map_y,
            0, 0, 128, 160);
	    cursor.x = 0;
	    cursor.x_old = 0;
            cursor.map_x += 128;
	}

      } else if (joy_baseHoriz - analogRead(JOY_HORIZ) > OFFSET) {
	// move left
	cursor.x -= INCREMENT;
	//at left edge of image
	if (cursor.x_global <= 2){
	  cursor.x_global = 2;
	  cursor.x = 2;
	  cursor.x_old = 2;
	}
	// near left edge of image
	if (cursor.x_global == 128){
	  lcd_image_draw(&map_image, &tft, 0, cursor.map_y, 
	  0, 0, 128, 160);
	  cursor.map_x = 0;
	  cursor.x = 126;
	  cursor.x_old = cursor.x;
	  cursor.x_global = 126;
	}
	// hit left edge of screen
	if (cursor.x <= -1){
	  lcd_image_draw(&map_image, &tft, cursor.map_x-128, cursor.map_y,
          0, 0, 128, 160);
	  cursor.x = tft.width();
	  cursor.x_old = tft.width();
	  cursor.map_x -= 128;
	}
      }

      if (analogRead(JOY_VERT) - joy_baseVert > OFFSET) {
	// move down
	cursor.y += INCREMENT;
	// hit bottom of image
	if (cursor.y_global >= 2046){
	  cursor.y_global = 2046;
	  cursor.y = 158;
	  cursor.y_old = 158;
	}
	// near bottom of image
	if (cursor.y_global == 1888){
	  lcd_image_draw(&map_image, &tft, cursor.map_x, 1888, 
	  0, 0, 128, 160);
	  cursor.map_y = 1888;
	  cursor.y = 2;
	  cursor.y_old = cursor.y;
	  cursor.y_global = 1890;
	}
	// hit bottom edge of screen
	if (cursor.y >= tft.height()+1){
           lcd_image_draw(&map_image, &tft, cursor.map_x, cursor.map_y+160,
           0, 0, 128, 160);
	   cursor.y = 0;
	   cursor.y_old = 0;
	   cursor.map_y += 160;

	}

      } else if (joy_baseVert - analogRead(JOY_VERT) > OFFSET) {
	// move up
	cursor.y -= INCREMENT;
	// hit top of image
	if (cursor.y_global <= 2){
	  cursor.y_global = 2;
	  cursor.y = 2;
	  cursor.y_old = 2;
	}
	// near top of image
	if (cursor.y_global == 160){
	  lcd_image_draw(&map_image, &tft, cursor.map_x, 0, 
	  0, 0, 128, 160);
	  cursor.map_y = 0;
	  cursor.y = 158;
	  cursor.y_old = cursor.y;
	  cursor.y_global = 158;
	}
	// hit top edge of screen
	if (cursor.y <= -1){
	  lcd_image_draw(&map_image, &tft, cursor.map_x, cursor.map_y-160,
          0, 0, 128, 160);
	  cursor.y = tft.height();
	  cursor.y_old = tft.height();
	  cursor.map_y -= 160;
 
	}
      }

      // Redraw map
      lcd_image_draw(&map_image, &tft,
		     cursor.x_global, cursor.y_global,      // upper-left corner of IMAGE
		     cursor.x_old, cursor.y_old,      // upper-left corner of SCREEN
		     cursor.width, cursor.height); // size of patch drawn
      // Draw new cursor
      tft.fillRect(cursor.x, cursor.y,
		   cursor.width, cursor.height, ST7735_MAGENTA);
    } else {
      /* Display List Mode */
      RestDist restDists[NUM_RESTAURANTS];
      int scroll_index;
      if (!sorted) {
	/*
	  Load, Sort and Display Restaurants near our Location
	*/
	tft.fillScreen(COLOR_BG);

	scroll_index = 0;

	/* Get cursor loc */
	int32_t cursorLon = x_to_lon(cursor.x);
	int32_t cursorLat = y_to_lat(cursor.y);

	/* Load RestDist Structs from SD Card */
	Restaurant r;
	for (int i=0; i < NUM_RESTAURANTS; i++) {
	  get_restaurant_fast(i, &r);
	  int32_t distanceLon = abs(cursorLon - r.longitude);
	  int32_t distanceLat = abs(cursorLat - r.latitude);
	  restDists[i].index = i;
	  restDists[i].dist = distanceLon + distanceLat;
	  restDists[i].rating = r.rating;
	}
	/* Sort List */
	selection_sort(restDists);
	Serial.println("Sorted.");
	
	/* Display Restaurant Names */
	print_restaurants(restDists, RESTAURANTS_TO_DISPLAY, min_rating, scroll_index);
		
	sorted = true;
      } else {
	/* Handle List Scrolling */
	if (joy_baseVert - analogRead(JOY_VERT) > OFFSET) {
	  // scroll up the list
	  if (scroll_index > 0) {
	    scroll_index--;
	  }
	  print_restaurants(restDists, RESTAURANTS_TO_DISPLAY, min_rating, scroll_index);
	} else if (analogRead(JOY_VERT) - joy_baseVert > OFFSET) {
	  // scroll down the list
	  if (scroll_index < NUM_RESTAURANTS - RESTAURANTS_TO_DISPLAY) {
	    scroll_index++;
	  }
	  print_restaurants(restDists, RESTAURANTS_TO_DISPLAY, min_rating, scroll_index);
	}
      }
    }

    delay(50);//ms
  }

}

/* Constants for Map Size */
const int16_t map_width = 2048;
const int16_t map_height = 2048;
const int32_t map_north = 5359942;
const int32_t map_west = -11365028;
const int32_t map_south = 5345428;
const int32_t map_east = -11340367;

int32_t x_to_lon(int16_t x) {
  return map(x, 0, map_width, map_west, map_east);
}
int32_t y_to_lat(int16_t y) {
  return map(y, 0, map_height, map_north, map_south);
}
int16_t lon_to_x(int32_t lon) {
  return map(lon, map_west, map_east, 0, map_width);
}
int16_t lat_to_y(int32_t lat) {
  return map(lat, map_north, map_south, 0, map_height);
}

/* Global Variables for this function:

   Restaurant restBuffer[8],
   int lastRestBlock;
   
   are declared underneath RestDist struct */
void get_restaurant_fast(int i, Restaurant *r) {
  uint16_t blocknum = i / 8;
  if (blocknum == lastRestBlock) {
    // do nothing, the buffer is already in restBuffer
  } else {
    // read the new buffer
    card.readBlock(RESTAURANT_START_BLOCK + blocknum, (uint8_t *)restBuffer);
  }
  lastRestBlock = blocknum;
  (*r) = restBuffer[i%8];
}

/* Swaps 2 RestDist elements in a RestDist array */
void swap_restDist(RestDist *restDists, int i, int j) {
  RestDist tmp = restDists[i];
  restDists[i] = restDists[j];
  restDists[j] = tmp;
}

/* Sorts a RestDist array based on dist, using selection sort */
void selection_sort(RestDist *restDists) {
  for (int i=0; i <= NUM_RESTAURANTS-2; i++) {
    for (int j=i+1; j <= NUM_RESTAURANTS-1; j++) {
      if (restDists[j].dist < restDists[i].dist) {
	swap_restDist(restDists, i, j);
      }
    }
  }
}


/* Print Restaurants
   
   Prints restaurants nearest the cursor to the LCD TFT screen.
    - must be at least the minimum rating specified by the dial
    - can be scrolled (3/4 screen at a time instead of whole screen to preserve
      continuity)
 */
void print_restaurants(RestDist* restDists, int restaurants_to_display, int min_rating, int scroll_index) {
  tft.fillScreen(COLOR_BG);
  tft.setCursor(0,0);
  tft.setTextColor(ST7735_BLACK);
  int displayed_restaurants = 0;
  Restaurant r;
  int num_skipped = 0;
  int scroll_dist = scroll_index*(restaurants_to_display*3/4);
  for (int i=0; displayed_restaurants < restaurants_to_display; i++) {
    get_restaurant_fast(restDists[i].index, &r);
    // Only print the restaurant if its rating is higher than
    // min_rating (selected by the dial)
    if ( (r.rating+1)/2 >= min_rating) {
      // If we are still scrolling, skip printing this one.
      if (num_skipped < scroll_dist) {
	num_skipped++;
	continue;
      }
      // Print the first CHARS_PER_LINE characters
      for (int j=0; j < CHARS_PER_LINE; j++) {
	tft.print(r.name[j]);
      }
      tft.println();
      displayed_restaurants++;
    }
  }
}


void loop() {
  // unused
}
