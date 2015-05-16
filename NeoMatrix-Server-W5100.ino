#include <SPI.h>
#include <Ethernet.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
#include <elapsedMillis.h>
#include <avr/wdt.h>

//#define DEBUG

#ifdef DEBUG
#define Debug(message) Serial.println(F(message))
#else
#define Debug(message)
#endif


#define NEOMATRIX_COLUMNS 31
#define NEOMATRIX_ROWS 8
#define NEOMATRIX_CONTROL_PIN 8
#define CHARACTER_WIDTH 6

/* *********************************
 * Functions and types declarations
 * *********************************/
typedef enum HTTPState
{
    HTTP_IDLE,            /* Waiting for request */
    HTTP_PARSED_HEADERS,  /* Headers parsed  */
    HTTP_PARSED_CONTENT,  /* Content parsed */
    HTTP_PARSED_REQUEST,  /* Request parsed */
    HTTP_ERROR            /* Error. Run away! */
};

void HTTP_parse_request(EthernetClient client);
void HTTP_parse_headers(EthernetClient client, int* content_length);
void HTTP_parse_content(EthernetClient client, int content_length);
void HTTP_send_204(EthernetClient client);
void HTTP_send_500(EthernetClient client);
void content_parser(char c, int position, int length);
void content_parser_init(void);

/* *****************
 * Global variables
 * *****************/

/* Settings for Ethernet */
byte mac[] = { 0x66, 0xff, 0xaa, 0xcc, 0x31, 0x32 };
IPAddress ip(192, 168, 1, 100);
EthernetServer server(80);
HTTPState server_state;

Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(
	/* Columns:  */  NEOMATRIX_COLUMNS,
	/* Rows:     */  NEOMATRIX_ROWS,
	/* Data Pin: */  NEOMATRIX_CONTROL_PIN,
	NEO_MATRIX_TOP     + NEO_MATRIX_RIGHT +
	NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
	NEO_GRB            + NEO_KHZ800);

/* Message */
typedef struct display {
	uint16_t color;
	char text[32];
    uint8_t length;
    uint8_t speed;
	bool update;
} display_t;

display_t current_display;

int cursor_pos    = matrix.width();
elapsedMillis timeElapsed;
int interval = 0;

int msglen(int length) {
	if (length * CHARACTER_WIDTH < NEOMATRIX_COLUMNS)
		return NEOMATRIX_COLUMNS * 2;
	else
	    return (NEOMATRIX_COLUMNS * 2) + ((length * CHARACTER_WIDTH) - NEOMATRIX_COLUMNS);
}


int freeRam () {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

int ctoi(char c){
	if (c >= 48 && c <= 57)
	    /* 48 - 57 : 0-9 */
		return c - 48;
	if (c >= 97 && c <= 102)
	    /* 97 - 102 : a-f */
		return c - 97 + 10;
	if (c >= 65 && c <= 70)
	    /* 65 -  70 : A-F */
	    return c - 65 + 10;
	return -1;
}

int strhex(char *str) {
	int val, retval = 0;
	char *str_i;
	for (str_i = str; *str_i != '\0'; str_i++) {
	    val = ctoi(*str_i);
		if (val>=0)
		    retval = (retval * 16) + val;
	}
	return retval;
}

int strint(char *str) {
	int val, retval = 0;
	char *str_i;
	for (str_i = str; *str_i != '\0'; str_i++) {
		val = ctoi(*str_i);
		if (val>=0)
		    retval = (retval * 10) + val;
	}
	return retval;
}

int strlength(char *str) {
	int retval=0;
	char *str_i;
	for(str_i = str; *str_i != '\0'; str_i++)
		retval++;
	return retval;
}

/* ************************
 * Arduino entry functions
 * ************************/
void setup()
{

#ifdef DEBUG
	Serial.begin(9600);
#endif

	/* Initialize the ethernet module*/
	Ethernet.begin(mac, ip);
	/* Start server */
	server.begin();

    Debug("Waiting for connections.");

	/* Initialize NeoMatrix */
	matrix.begin();
	matrix.setTextWrap(false);
	matrix.setBrightness(32);

	current_display.color = matrix.Color(0, 200, 0);
	strcpy(current_display.text, "Hello, world!");
	current_display.length = msglen(strlength(current_display.text));
	current_display.speed = 20;
	current_display.update = true;
	
	/* Enable watchdog */
	wdt_enable(WDTO_1S);
}

void loop()
{
	/* Listen for incoming connections */
	EthernetClient client = server.available();
	server_state = HTTP_IDLE;

    /* Feed watchdog */
    wdt_reset();
    
    /* Check RAM usage */
    if (freeRam() < 16) {
    	Debug("RAM is almost full");
    	/* HACK ALERT: Watchdog will get us outta here */
    	while(1);
    }
    
	if (client)
		/* Client connected */
	{
        Debug("New connection.");

		/* Initialize content parser */
		content_parser_init();
		HTTP_parse_request(client);
        Debug("Request parsed.");
		switch (server_state)
		{
				/* If the request succeeded, send HTTP 204 */
			case HTTP_PARSED_REQUEST:
                Debug("Sending 204");
                HTTP_send_204(client);
				break;
				/* Otherwise, send HTTP 500 */
			case HTTP_ERROR:
			default:
				Debug("Sending 500");
				HTTP_send_500(client);
		}

		/* Close the connection */
		client.stop();
		Debug("Connection closed.");
	}

    if (timeElapsed > interval) {
      /* Text need update ? */
      if(current_display.update) {
        matrix.setTextColor(current_display.color);
        if (current_display.speed > 0) {
            cursor_pos = matrix.width();
        	interval = 1000 / current_display.speed;
        }
        else {
        	cursor_pos = 0;
        	interval = 0;
        }
        current_display.update = false;
      }
      matrix.fillScreen(0);
      matrix.setCursor(cursor_pos, 0);
      matrix.print(current_display.text);
      if(interval > 0 && --cursor_pos < (NEOMATRIX_COLUMNS - current_display.length)) {
        cursor_pos = matrix.width();
      }
      matrix.show();
      timeElapsed = 0;
    }
}

/* ***********************
 * HTTP Handling Functions
 * ***********************/

/* Static keyword strings
 * XXX: These strings eat up the SRAM */
char HTTP_POST_METHOD_STR[]     = "POST";
char HTTP_CONTENT_LENGTH_STR[]  = "Content-Length";

void HTTP_parse_request(EthernetClient client)
{
	/* We should only reach here from the IDLE state */
	if (server_state != HTTP_IDLE)
	{
		/* Set ERROR status and return */
        Debug("Error: Invalid state.");
		server_state = HTTP_ERROR;
		return;
	}

	/* Since this is a new request we should parse
	 * the HTTP headers.
	 */
	int content_length = 0;
	HTTP_parse_headers(client, &content_length);
    Debug("Headers parsed.");

	/* We should now be in PARSED_HEADER state */
	if (server_state != HTTP_PARSED_HEADERS)
	{
		/* Set ERROR status and return */
        Debug("Error: Invalid state.");
		server_state = HTTP_ERROR;
		return;
	}

	/* If content-length is set non-zero we should now
	 * parse the request content
	 */
	if (content_length)
	{

		/* Parse the content! */
		HTTP_parse_content(client, content_length);
	    Debug("Content parsed.");
		/* We should now be in PARSED_CONTENT state */
		if (server_state != HTTP_PARSED_CONTENT)
		{
			/* Set ERROR status and return */
			Debug("Error: Invalid state.");
			server_state = HTTP_ERROR;
			return;
		}
	}
	server_state = HTTP_PARSED_REQUEST;
	return;
}

void HTTP_parse_headers(EthernetClient client, int *content_length)
{
	/* Parse the HTTP request headers.
	 * XXX: We only support POST method for now.
	 */

	/* Strings and pointers and fun */

	char *method_ptr = &HTTP_POST_METHOD_STR[0];
	char *clen_ptr = &HTTP_CONTENT_LENGTH_STR[0];
	char clen_val[8] = "\0\0\0\0\0\0\0";
	char *clen_val_i = &clen_val[0];
	
	/* Parser state */
	enum HTTPHeaderState
	{
	    HTTP_HEADER_METHOD = 0,
	    HTTP_HEADER_CONTENT_LENGTH,
	    HTTP_HEADER_COLON,
	    HTTP_HEADER_CONTENT_LENGTH_VAL,
	    HTTP_HEADER_PARSING_COMPLETE
	} header_state;

	boolean lineIsBlank = true;

	boolean found[2] = {false, false};

	/* First parse method */
	header_state = HTTP_HEADER_METHOD;

	while (client.connected())
	{
		if (client.available())
		{
			/* Read a character from client */
			char c = client.read();

			/* if we've reached the end of the line and the line is blank,
			 * the http headers has ended.
			 */
			if (c == '\n' && lineIsBlank)
			{
				if (found[HTTP_HEADER_METHOD]
				 && found[HTTP_HEADER_CONTENT_LENGTH])
				{
					server_state = HTTP_PARSED_HEADERS;
					*content_length = strint(clen_val);
					return;
				}
				else
				{
					Debug("Error: Header missing.");
					server_state = HTTP_ERROR;
					return;
				}
			}
			if (c == '\n')
			{
				/* A new line is starting */
				lineIsBlank = true;
			}
			else if (c != '\r')
			{
				/* There is a character in current line */
				lineIsBlank = false;
			}

			if (header_state == HTTP_HEADER_METHOD)
			{
				if (c == *method_ptr)
					method_ptr++;
				else
					method_ptr = &HTTP_POST_METHOD_STR[0];
				if (*method_ptr == '\0')
				{
					header_state = HTTP_HEADER_CONTENT_LENGTH;
					found[HTTP_HEADER_METHOD] = true;
					continue;
				}
			}
			if (header_state == HTTP_HEADER_CONTENT_LENGTH)
			{
				if (c == *clen_ptr)
					clen_ptr++;
				else
					clen_ptr = &HTTP_CONTENT_LENGTH_STR[0];
				if(*clen_ptr == '\0')
				{
					header_state = HTTP_HEADER_COLON;
					found[HTTP_HEADER_CONTENT_LENGTH] = true;
					continue;
				}
			}
			if (header_state == HTTP_HEADER_COLON)
			{
				if (c == ' ')
					continue;
				if (c == ':')
				{
					header_state = HTTP_HEADER_CONTENT_LENGTH_VAL;
					continue;
				}
				else
				{
					server_state = HTTP_ERROR;
					Debug("Error: Malformed header: Colon.");

					return;
				}
			}
			if (header_state == HTTP_HEADER_CONTENT_LENGTH_VAL)
			{
				if (c == ' ')
					continue;
				if ( c == '\r' || c == '\n')
				{
					header_state = HTTP_HEADER_PARSING_COMPLETE;
					continue;
				}
				if ( c > 47 && c < 58)
				{
					*clen_val_i = c;
					clen_val_i++;
				}
				else
				{
					server_state = HTTP_ERROR;
					Debug("Error: Malformed header: Value.");
					return;
				}
			}
		}
	}
}

void HTTP_parse_content(EthernetClient client, int content_length)
{
	int i, timeout;
	for(i = 0; i < content_length; i++)
	{
		if (!client.connected())
		{
			Debug("Error: Connection dropped.");
			server_state = HTTP_ERROR;
			return;
		}
		/* 1000 ms should be enough for everyone */
		timeout = 1000;
		while(!client.available())
		{
			delay(1);
			if(timeout-- == 0)
			{
				Debug("Error: Connection timeout.");
				server_state = HTTP_ERROR;
				return;
			}
		}
		/* Read a character from client */
		char c = client.read();

		content_parser(c);
	}

	server_state = HTTP_PARSED_CONTENT;
}

void HTTP_send_204(EthernetClient client)
{
	/* Send HTTP OK Status to client without content.
	 */
	client.println(F("HTTP/1.1 204 No Content\nServer: Arduino NeoMatrix/0.1\nCache-Control: no-cache\nConnection: close\n\n"));
}

void HTTP_send_500(EthernetClient client)
{
	/* Send HTTP Error Status to client without content.
	 */
	client.println(F("HTTP/1.1 500 Internal Server Error\nServer: Arduino NeoMatrix/0.1\nConnection: close\n\n"));
}


/*************************
 * JSON Handling functions
 *************************/

/* Static strings
 * XXX: These strings eat up the SRAM */
char CONTENT_JSON_FIELD_COLOR[] = "color";
char CONTENT_JSON_FIELD_MSG[]   = "text";
char CONTENT_JSON_FIELD_SPEED[] = "speed";

typedef enum ParserState
{
    PARSING_IDLE,        /* Consuming text */
    PARSING_COLOR_KEY,   /* Parsing color field  */
    PARSING_COLOR_VALUE,
    PARSING_TEXT_KEY,    /* Parsing message field */
    PARSING_TEXT_VALUE,
    PARSING_SPEED_KEY,   /* Parsing speed field */
    PARSING_SPEED_VALUE
};

typedef enum ParserExpect
{
    COLON,
    QUOTE,
    STRING
};

/* Use static pointers as iterators */
char *json_color;
char *json_text;
char *json_speed;

ParserState  json_parser_state;
ParserExpect json_parser_expect;

char json_color_field[7];
char json_text_field[33];
char json_speed_field[4];
char buffer[3] = "\0\0";
int characters_rd = 0;
int red;
int green;
int blue;



void content_parser_init(void) {
	Debug("Content parser init");
	/* Set state to idle */
	json_parser_state = PARSING_IDLE;
	/* Reset iterators */
	json_color = &CONTENT_JSON_FIELD_COLOR[0];
	json_text = &CONTENT_JSON_FIELD_MSG[0];
	json_speed = &CONTENT_JSON_FIELD_SPEED[0];
}

void content_parser(char c){
  Debug("Content parser");
  switch(json_parser_state) {
  	/*
  	 * Parser waiting for key. (Idle state)
  	 */
    case PARSING_IDLE:
    Debug("Parsing idle");
    if(c == *json_text) {
      json_text++;
      json_parser_state = PARSING_TEXT_KEY;
    }    
    else if(c == *json_color) {
      json_color++;
      json_parser_state = PARSING_COLOR_KEY;
    }
    else if(c == *json_speed) {
      json_speed++;
      json_parser_state = PARSING_SPEED_KEY;
    }
    break;

    /*
     * This looks like text field key.
     */
    case PARSING_TEXT_KEY:
    Debug("Parsing text key");
    if(c == *json_text) {
	  json_text++;
    }
	else {
	  Debug("Else text.");
	  json_text = &CONTENT_JSON_FIELD_MSG[0];
	  json_parser_state = PARSING_IDLE;
	}
	if (*json_text == '\0') {
	  /* Now parsing message */
	  Debug("Text field found.");
	  json_parser_state = PARSING_TEXT_VALUE;
	  json_parser_expect = COLON;
	}
    break;
    case PARSING_COLOR_KEY:
    if(c == *json_color) {
	  json_color++;
    }
	else {
	  json_color = &CONTENT_JSON_FIELD_COLOR[0];
	  json_parser_state = PARSING_IDLE;
	}
	if (*json_color == '\0') {
	  /* Now parsing color */
	  Debug("Color field found.");
      json_parser_state = PARSING_COLOR_VALUE;
	  json_parser_expect = COLON;
	}
    break;
    case PARSING_SPEED_KEY:
    if(c == *json_speed) {
	  json_speed++;
    }
	else {
	  json_speed = &CONTENT_JSON_FIELD_SPEED[0];
	  json_parser_state = PARSING_IDLE;
	}
    if (*json_speed == '\0') {
      /* Now parsing speed */
	  Debug("Speed field found.");
	  json_parser_state = PARSING_SPEED_VALUE;
	  json_parser_expect = COLON;
	}
    break;

	case PARSING_COLOR_VALUE:
	  switch (json_parser_expect) {
	  	case COLON:
	  		if ( c == ':' )
	  			json_parser_expect = QUOTE;
	  		break;
	  	case QUOTE:
	  	    if ( c == '\"')
	  	    	json_parser_expect = STRING;
	  	    	characters_rd = 0;
	  	    break;
	  	case STRING:
	  	    if (++characters_rd > 6) {
	  	    	/* Oops. String too long */
	  	        json_parser_state = PARSING_IDLE;
	  	        json_color = &CONTENT_JSON_FIELD_COLOR[0];
	  	    }
	  	    json_color_field[characters_rd - 1] = c;
	  	    if ( c == '\"') {
	  	    	/* String read success*/
	  	    	json_color_field[characters_rd - 1] = '\0';
	  	    	/* Parse red */
	  	    	buffer[0] = json_color_field[0];
	  	    	buffer[1] = json_color_field[1];
	  	    	red = strhex(buffer);
	            /* Parse green */
	            buffer[0] = json_color_field[2];
	  	    	buffer[1] = json_color_field[3];
	  	    	green = strhex(buffer);
	            /* Parse blue */
	            buffer[0] = json_color_field[4];
	  	    	buffer[1] = json_color_field[5];
	  	    	blue = strhex(buffer);
	  	    	current_display.color = matrix.Color(red, green, blue);
	  	    	current_display.update = true;
	  	        json_parser_state = PARSING_IDLE;
	  	        json_color = &CONTENT_JSON_FIELD_COLOR[0];
	  	    }
	  	    break;
	  }
	  break;
	case PARSING_TEXT_VALUE:
	  switch (json_parser_expect) {
	    case COLON:
	  		if ( c == ':' )
	  			json_parser_expect = QUOTE;
	  		break;
	  	case QUOTE:
	  	    if ( c == '\"')
	  	    	json_parser_expect = STRING;
	  	    	characters_rd = 0;
	  	    break;
	  	case STRING:
	  	    if (++characters_rd > 32) {
	  	    	/* Oops. String too long */
	  	        json_parser_state = PARSING_IDLE;
	  	        json_text = &CONTENT_JSON_FIELD_MSG[0];
	  	        
	  	    }
	  	    json_text_field[characters_rd - 1] = c;
	  	    if ( c == '\"') {
	  	    	/* String read success*/
	  	    	json_text_field[characters_rd - 1] = '\0';
	  	    	
	  	    	strcpy(current_display.text, json_text_field);
	  	        current_display.length = msglen(strlength(current_display.text));
	  	    	current_display.update = true;
	  	        json_parser_state = PARSING_IDLE;
	  	        json_text = &CONTENT_JSON_FIELD_MSG[0];
	  	    }
	  	    break;
	  }
	  break;

	case PARSING_SPEED_VALUE:
	  switch (json_parser_expect) {
	  	case COLON:
	  		if ( c == ':' )
	  			json_parser_expect = QUOTE;
	  		break;
	  	case QUOTE:
	  	    if ( c == '\"')
	  	    	json_parser_expect = STRING;
	  	    	characters_rd = 0;
	  	    break;
	  	case STRING:
	  	    if (++characters_rd > 3) {
	  	    	/* Oops. String too long */
	  	        json_parser_state = PARSING_IDLE;
	  	        json_speed = &CONTENT_JSON_FIELD_SPEED[0];
	  	    }
	  	    json_speed_field[characters_rd - 1] = c;
	  	    if ( c == '\"') {
	  	    	/* String read success*/
	  	    	json_speed_field[characters_rd - 1] = '\0';
	  	    	current_display.speed = strint(json_speed_field);
	  	    	current_display.update = true;
	  	        json_parser_state = PARSING_IDLE;
	  	        json_speed = &CONTENT_JSON_FIELD_SPEED[0];
	  	    }
	  	    break;
	  }
	  break;
    }
}