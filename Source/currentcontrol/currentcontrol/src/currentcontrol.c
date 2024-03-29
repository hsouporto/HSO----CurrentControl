/*
 * currentcontrol.c
 *
 * Created: 07-09-2015 14:59:44
 *  Author: Nelson
 */ 

#define F_CPU 16000000
#define DEBUG

#include <avr/io.h>
#include <math.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include "libs/utils/bit_tools.h"
#include "libs/utils/my_utils.h"
#include "libs/pid/pid.h"
#include "libs/interrupts/interruptvectors.h"
#include "libs/usart/usart.h"
#include "libs/adc/adc_analog.h"
#include "libs/timer/timer_utils.h"


// GLOBAL variables
char bufferDummy[30];
extern char usartBuffer[USART_BUFFER_LENGHT];

extern char tokenParts[15][20];


/************************************************************************/
/* Flags                                                                     */
/************************************************************************/
// flags
volatile uint8_t  flagCurrentEnable;

volatile uint8_t flagTaskReadButtons =0;
volatile uint8_t flagTaskControl =0;
volatile uint8_t flagTaskUsartMessage=0;
volatile uint8_t flagSaveParameters=0;

// Task periocity 1ms base tick
volatile uint16_t taskReadButtonsPeriod = 60;
volatile uint16_t taskControlPeriod = 10;



// STATE MACHINE
#define STATE_IDLE 0
#define STATE_PROGRAM 1
#define STATE_SAVE 2

#define IDLE_LED_ON bit_clear(PORTC,4)
#define IDLE_LED_OFF bit_set(PORTC,4)
#define PRGM_LED_ON bit_clear(PORTC,3)
#define PRGM_LED_OFF bit_set(PORTC,3)


#define BUTTON_PROG 1
#define BUTTON_ENTER 2
#define BUTTON_UP 3
#define BUTTON_DOWN 4
volatile uint8_t state= STATE_IDLE; // by default



/************************************************************************/
/* PID STUFF                                                                     */
/************************************************************************/
#define N_MSG_TOKENS 11 // CHECK NUMBER
// PID HARDCODE DEFINES
#define PIDP_DEFAULT 1.5
#define PIDD_DEFAULT 0.0
#define PIDI_DEFAULT 0.2
#define PIDP_EMAX_DEFAULT 12000
#define PIDP_EMIN_DEFAULT -12000
#define PIDI_EMAX_DEFAULT 12000
#define PIDI_EMIN_DEFAULT -12000


#define OFFSET_DEFAULT 0
#define SETPOINT_DEFAULT 0
#define SETPOINTIDLETHRESHOLDOFFSETDOWN_DEFAULT -50
#define SETPOINTIDLETHRESHOLDOFFSETUP_DEFAULT 110

// poupular resto de parameteris


// GLOBAL

volatile float pidP;
volatile float pidD;
volatile float pidI;
volatile int16_t pidPerrMin;
volatile int16_t pidPerrMax;
volatile int16_t pidIerrMin;
volatile int16_t pidIerrMax;
volatile int16_t offset;
volatile int16_t setpoint;
volatile int16_t setpointidlethresholdoffsetdown;
volatile int16_t setpointidlethresholdoffsetup;


// Slope curve
#define SLOPE_INC 0.1
#define SLOPE_DEC -0.1
#define SLOPE_MAX_VAL 2.0
#define SLOPE_MIN_VAL 0.9
#define SLOPE_VAL_DEFAULT 1.0

double slopeValue = SLOPE_VAL_DEFAULT; // default value




// pid variables
typedef struct{
	uint8_t initEeprom;
	int16_t pidPerrMin;
	int16_t pidPerrMax;
	int16_t pidIerrMin;
	int16_t pidIerrMax;
	float pidP;
	float pidD;
	float pidI;
	int16_t offset;
	int16_t setpoint;
	int16_t setpointidlethresholdoffsetdown;
	int16_t setpointidlethresholdoffsetup;
	
	double slopeValue;	
	
	
}eestruct_t;


eestruct_t EEMEM eestruct_eemem;
eestruct_t eestruct_var;





/************************************************************************/
/* SCHEDULER STUFF                                                                     */
/************************************************************************/
// Timer setup for control loop
// freq = (FCPU /prescaler) /timerscale
// timerscale timer0 8bits = 256
// 
#define TIMER0_TICK 0.001
#define TIMER0_SCHED_PRESC TIMER0_PRESC128
#define TIMER0_SCHED_RELOAD 256 -156 // TOP or 250 for Botton

#define SAMPLING_PERIOD 0.002 // 1ms base time
volatile uint16_t schedulerMaxCount=5000;

// PWM
#define CURRENT_MAXPWM 1600 // VALOR PWM 16Bits // aqui pode-se ajustar
#define CURRENT_MINPWM 0
#define CURRENT_IDLEPWM 0


// LOCAL PROTOTIPES
void stateMachine(uint8_t inCode);
void setSlope(uint8_t key);

void schedulerInit(void);
void pwmInit(void);
void controlInit(void);
uint16_t controlLoop(void);
void configGPIO(void);
uint8_t decodeButton(uint8_t button);

void paramLoadDefaultParameters(void);

void eepromSetDefaultParameters(void);

void paramSavetoEeprom(void);
void paramLoadFromEeprom(void);


/************************************************************************/
/* load default parameters                                                                     */
/************************************************************************/
void paramLoadDefaultParameters(void){
	pidP = PIDP_DEFAULT;
	pidD = PIDD_DEFAULT;
	pidI = PIDI_DEFAULT;
	pidPerrMax = PIDP_EMAX_DEFAULT;
	pidPerrMin = PIDP_EMIN_DEFAULT;
	pidIerrMax = PIDI_EMAX_DEFAULT;
	pidIerrMin = PIDI_EMIN_DEFAULT;
	offset = OFFSET_DEFAULT;
	setpoint = SETPOINT_DEFAULT;
	setpointidlethresholdoffsetdown = SETPOINTIDLETHRESHOLDOFFSETDOWN_DEFAULT;
	setpointidlethresholdoffsetup = SETPOINTIDLETHRESHOLDOFFSETUP_DEFAULT;
	
	slopeValue =SLOPE_VAL_DEFAULT;
}


/***************************************************
// MESAGE FORMAT:
// 1.23P,1.01I,0.02D,-1000EPm,1000EPM,-10000EIm,10000EIM,5000O,300S,1000TD,2000TU,1.2S,|


/***************************************************
/************************************************************************/
/* @try to parse the tokens to the variables                                                                     */
/************************************************************************/
uint8_t paramConvertFromTokens(uint8_t n){
	
	
	
	if(n == N_MSG_TOKENS){
		
		pidP = atof(tokenParts[0]);
		pidI = atof(tokenParts[1]);
		pidD = atof(tokenParts[2]);
		
		pidPerrMin = atoi(tokenParts[3]);
		pidPerrMax = atoi(tokenParts[4]);
		pidIerrMin = atoi(tokenParts[5]);
		pidIerrMax = atoi(tokenParts[6]);
		
		
		offset = atoi(tokenParts[7]);
		setpoint = atoi(tokenParts[8]);
		setpointidlethresholdoffsetdown = atoi(tokenParts[9]);
		setpointidlethresholdoffsetup = atoi(tokenParts[10]);
		slopeValue = atof(tokenParts[11]);


		
		return 1; // all parseed 
	}
	
	return 0; // message invalid
	
	
	
}


/************************************************************************/
/* @ set initial values to eeprom  if nothin there yet                                                                   */
/************************************************************************/
void eepromSetDefaultParameters(){
	eestruct_var.initEeprom=1; // emprom init
	
	eestruct_var.pidP = PIDP_DEFAULT;
	eestruct_var.pidI = PIDI_DEFAULT;
	eestruct_var.pidD = PIDD_DEFAULT;
	eestruct_var.pidPerrMin = PIDP_EMIN_DEFAULT;
	eestruct_var.pidPerrMax = PIDP_EMAX_DEFAULT;
	eestruct_var.pidIerrMin = PIDI_EMIN_DEFAULT;
	eestruct_var.pidIerrMax = PIDI_EMAX_DEFAULT;
	eestruct_var.offset = OFFSET_DEFAULT;
	eestruct_var.setpoint = SETPOINT_DEFAULT;
	eestruct_var.setpointidlethresholdoffsetdown = SETPOINTIDLETHRESHOLDOFFSETDOWN_DEFAULT;
	eestruct_var.setpointidlethresholdoffsetup = SETPOINTIDLETHRESHOLDOFFSETUP_DEFAULT;
	
	eestruct_var.slopeValue = SLOPE_VAL_DEFAULT;
	
	eeprom_write_block((const void*)&eestruct_var,(void*)&eestruct_eemem,sizeof(eestruct_t));
	
}


/************************************************************************/
/* @restore to EEPROM                                                                     */
/************************************************************************/
void paramLoadFromEeprom(){
uint8_t temp=0;	
	// read from emprom
	eeprom_read_block((void*)&eestruct_var, (const void*)&eestruct_eemem,sizeof(eestruct_t));
	
	// test the fits field to check if it was written else use default and load
	if((eestruct_var.initEeprom &0xFF) ==0xFF){
		eepromSetDefaultParameters();
		paramLoadDefaultParameters();
		
	}
	else{
		// write to the global variables
		pidP = eestruct_var.pidP;
		pidI = eestruct_var.pidI;
		pidD = eestruct_var.pidD;
		pidPerrMin = eestruct_var.pidPerrMin;
		pidPerrMax = eestruct_var.pidPerrMax;
		pidIerrMin = eestruct_var.pidIerrMin;
		pidIerrMax = eestruct_var.pidIerrMax;
		offset = eestruct_var.offset;
		setpoint = eestruct_var.setpoint;
		setpointidlethresholdoffsetdown = eestruct_var.setpointidlethresholdoffsetdown;
		setpointidlethresholdoffsetup = eestruct_var.setpointidlethresholdoffsetup;
		
		slopeValue = eestruct_var.slopeValue;
		
	}
	
		
	
}
/************************************************************************/
/* @read from EEPROM                                                                     */
/************************************************************************/
void paramSavetoEeprom(){
	
	
	// save paramenetrs on the run
	eestruct_var.initEeprom=1; // emprom init
	eestruct_var.pidP = pidP;
	eestruct_var.pidI = pidI;
	eestruct_var.pidD = pidD;

	eestruct_var.pidPerrMin = pidPerrMin;
	eestruct_var.pidPerrMax = pidPerrMax;
	eestruct_var.pidIerrMin = pidIerrMin;
	eestruct_var.pidIerrMax=pidIerrMax;
	
	eestruct_var.offset = offset;
	eestruct_var.setpoint = setpoint;
	eestruct_var.setpointidlethresholdoffsetdown = setpointidlethresholdoffsetdown;
	eestruct_var.setpointidlethresholdoffsetup = setpointidlethresholdoffsetup;
	
	eestruct_var.slopeValue=slopeValue;
	
	eeprom_write_block((const void*)&eestruct_var,(void*)&eestruct_eemem,sizeof(eestruct_t));
	
	
}




/************************************************************************/
/* @sate machine                                                                     */
/************************************************************************/
void stateMachine(uint8_t inCode){
	
uint8_t newVal;
uint8_t lastVal;
uint8_t count=0;

	inCode &= 0x07; // ensure clean
	
	
	
	// Switch to the state
	switch (state){
		
		case STATE_IDLE:
				IDLE_LED_ON;
				PRGM_LED_OFF;
				if (inCode == BUTTON_PROG) state=STATE_PROGRAM;
				break;
			
		case STATE_PROGRAM:
				IDLE_LED_OFF;
				PRGM_LED_ON; 
				if(inCode==BUTTON_UP)setSlope(1);
				if(inCode==BUTTON_DOWN)setSlope(0);
				if(inCode==BUTTON_ENTER){
					 state=STATE_SAVE;
					 //state=STATE_IDLE;
					 //IDLE_LED_ON;
					 //PRGM_LED_OFF;
				}
				break;
		case STATE_SAVE:
				IDLE_LED_ON;
				PRGM_LED_ON;
				flagSaveParameters++;
				//wait to be saved and exit
				state=STATE_IDLE;
				break;
			
		default:
				
			break;	
	}
}



/************************************************************************/
/* @set slope curve      0, decreases, 1 increases                                                               */
/************************************************************************/
void setSlope(uint8_t key){
	
	if (key){
		 slopeValue +=SLOPE_INC;
		 if(slopeValue >SLOPE_MAX_VAL) slopeValue = SLOPE_MAX_VAL;
	}
	if (!key)slopeValue +=SLOPE_DEC;
	if(slopeValue <SLOPE_MIN_VAL) slopeValue = SLOPE_MIN_VAL;
}


//dtostrf ((float)rhTrueValue, 3, 2, buffer); // avoid float printf

//



/************************************************************************/
/* Sheduler config                                                                     */
/************************************************************************/
void schedulerInit(void){
	
	TCCR0 |= TIMER0_SCHED_PRESC;
	TCNT0 |= TIMER0_SCHED_RELOAD; // timer count reaload
	TIMSK |= (1<< TOIE0); // Enable timer interrupt
	
}


/************************************************************************/
/* @ setup pwm ate 100us 16bits botton                                                                     */
/************************************************************************/
void pwmInit(void){
	/*
	The formula for Fast PWM
	F(PWM) = F(Clock)/(N*(1+TOP)
	*/
	// max 245 Hz at 16mhz with full top
	
	TCCR1B =0;
	TCCR1A =0;
	TCCR1A |= (1<<COM1B1) |(1<<WGM11);
	TCCR1B |= (1<<WGM13) | (1<<CS10); // CHECK! 
	
	ICR1 = CURRENT_MAXPWM;
	
	
}


/************************************************************************/
/* @Control init                                                                   */
/************************************************************************/
void controlInit(void){		

	PID_setPid(pidP,pidI,pidD);
	// set pid Limits
	
	// change this and call after params Load
	//PID_setLimitsIerr(pidIerrMin,pidIerrMax); 
	PID_setLimitsIerr(-12000,12000); 
}

/************************************************************************/
/* @control loop                                                                     */
/************************************************************************/
uint16_t controlLoop(void){
int16_t setValue=0;
int16_t feedbackValue=0;
static int16_t  old_pwm=0;	
	// read adc
	setValue = ADC_readAndWAIT(0);
	feedbackValue = ADC_readAndWAIT(1);
	// apply slope formula to the feedback curve
	
	//feedbackValue = (uint16_t)((double)feedbackValue*slopeValue);	
	
	/*#ifdef DEBUG
	sprintf(bufferDummy,"%4x, %4x\n\r",setValue,feedbackValue);
	USART1_sendStr(bufferDummy);
	#endif*/
	
	
	// Calculate error
	int16_t pidret = PID_update(setValue,feedbackValue, 1);
	
	int16_t newpwm = old_pwm + pidret;
	
	old_pwm = newpwm;
	
	// check for limits
	if (newpwm < CURRENT_MINPWM) newpwm = CURRENT_MINPWM;
	else if (newpwm > CURRENT_MAXPWM) newpwm = CURRENT_MAXPWM;
	
/*
// 	#ifdef DEBUG
// 	sprintf(bufferDummy,"%4x\n\r",newpwm);
// 	USART1_sendStr(bufferDummy);
// 	#endif
*/
	return newpwm;
}
/************************************************************************/
/*@Config GPIO                                                          */
/************************************************************************/
void configGPIO(void){
	
	//DDRF = 0x00;
	DDRB = 0xFF;
	PORTB = 0x00;
	DDRC =  0b00011000; 
	PORTC =	0b11111111; // ENABLE PULLUPS
	DDRD |= 0xfe;
	
}

/************************************************************************/
/* @decode Button                                                                     */
/************************************************************************/

uint8_t decodeButton(uint8_t button){
	button &=0x07;
	switch (button){
		
		case 1: return BUTTON_ENTER;
			break;
		
		case 2: return BUTTON_UP;
			break;	
		
		case 4: return BUTTON_DOWN;
			break;
			
		case 7: return BUTTON_PROG;//	
	
			
		default: return 0;
			break;	
	}
}



#define N_DEBOUNCE 3
/************************************************************************/
/* @debounce function                                                                     */
/************************************************************************/
uint8_t debounceKey(uint8_t codeNew){
	uint8_t key =0; // by default
	static codeOld;
	static keyCount;
	
	// ALREADY SOMETHIN PRESSED
	if(keyCount != 0){
		
		// IF SAME KEY and inside debounce times save
		if(codeNew == codeOld && keyCount <N_DEBOUNCE){ // ONLY IF EQUAL AND DEBOUNCE AVAILABLE
			codeOld =codeNew;
			keyCount++;
			// Reached debounce value and valid key
			if (keyCount == N_DEBOUNCE){
				key = codeNew; // ONLY HERE key is changed;
			
			}
		}
		
	}

		
	// INITIAL CONDITION
	if (keyCount == 0){
		codeOld = codeNew;
		keyCount++;
	}
		
	// if pressed key different reset (user must release the key for new run)
	if(codeNew != codeOld){
		codeOld =codeNew;
		keyCount =1;
	}
	return key;	
}


/************************************************************************/
/* @main                                                                      */
/************************************************************************/
int main(void){
uint16_t pwm=0;
uint8_t portVal=0;


	//1.  config stuffs
	USART1_config(USART1_MY_UBBRN,USART_DATA_FORMAT_8BITS|USART_STOP_BITS_1,USART_TRANSMIT_ENABLE|USART_RECEIVE_ENABLE| USART_INTERRUPT_ENABLE);
	
	ADC_init(ADC_ENABLE,ADC_REF_VCC,ADC_calcPreScaler(ADC_MAX_FREQ));
	configGPIO();
	schedulerInit();
	pwmInit();
	
	
	
	// call the memory read here
	paramLoadFromEeprom();
	
	//paramLoadDefaultParameters(); // 
	controlInit();
	
	
	USART1_sendStr("Hello\n\r");
	
	
	state= STATE_IDLE; // by default
	IDLE_LED_ON;
	PRGM_LED_OFF;
	
	//2. enable interrups
	sei();
	
	flagCurrentEnable =1;
	
	// set run status
	
	
	
	//3. loop
    while(1){
		
		 
		//1. buttons read
		if(flagTaskReadButtons){
				
			uint8_t codeNew;
			//portVal=0; // just
			portVal = (~(PINC & 0x07)&0x07); // Handle inverted logic
			
			codeNew = decodeButton(portVal);
			codeNew = debounceKey(codeNew);
		
			
			#ifdef DEBUG
			sprintf(bufferDummy,"%x\n\r",codeNew);
			USART1_sendStr(bufferDummy);
			#endif	
			
			stateMachine(codeNew); 

			flagTaskReadButtons=0;
		}
		

		//1. if flag activated save running parameters to eeprom
		if(flagSaveParameters){
			
			paramSavetoEeprom();
			
			flagSaveParameters=0;
			state = STATE_IDLE;
		}
		
		
		
		//2. Control loop to be esecuted
		if(flagTaskControl){
	
			pwm = controlLoop();
			flagTaskControl=0;
			
			/*#ifdef DEBUG
			sprintf(bufferDummy,"%x\n\r",pwm);
			USART1_sendStr(bufferDummy);
			#endif*/
		}
		
		//3. Apply pwm to the output
		if(flagCurrentEnable){
			// Update ocr 16 bits
			OCR1B= pwm;
		
		}else OCR1B =0;
		
		
		//4. new message arrive
		if(flagTaskUsartMessage){
			
			// tokenize the message
			uint8_t nTokens = parseString(usartBuffer,',');
			uint8_t val = paramConvertFromTokens(nTokens);
			
			if(val){
				
				// set the running values to
				PID_setPid(pidP,pidI,pidD);
				PID_setLimitsPerr(pidPerrMin,pidPerrMax);
				PID_setLimitsIerr(pidIerrMin,pidIerrMax);
				
				// activate to save running values to eeprom
				flagSaveParameters++;
				
			}
			flagTaskUsartMessage=0;
		}
       
    }
}
