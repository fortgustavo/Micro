/**
 * @brief Controlador de temperatura para aquecedor com histerese
 * @file main.c
 * @author João Guilherme Iwamoto
 * @author Gustavo Henrique Fortunato Medis
 * @version 0.1
 * @date 2022-05-24
 */


#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>
#include "teclado.h"
#include "lcd4bits.h"
#include "i2c.h"
#include "ds1307.h"
#include "eeprom.h"
#include "functions.h"


/**
 * @brief Variaveis globais
 * 
 */
unsigned char dia, mes, ano, hora, minuto, segundo;
unsigned char temp_LM[5], tecla,  timeout=0;
unsigned int contador_5s = 500;
unsigned char setpoint, setpoint_read, hr_on, hr_off, min_on, min_off;
uint8_t aux_tela = 0, aux_5seg = 0; temperatura;
int mins_start, mins_stop, mins_now;


/**
 * @brief Converter para bcd
 * 
 * @param vector 
 * @return uint8_t 
 */
uint8_t converter_bcd (uint8_t* vector) {
	return (vector[0] - 0x30) << 4 | (vector[1] - 0x30);
}


/**
 * @brief Funcao que le teclas do teclado matricial
 * 
 * @return uint8_t 
 */
uint8_t get_valores(){

	int i = 0;
	int dado;
	unsigned char str[2];
	
	inic_tc0();

	while (i < 2 && !timeout) {

		// Leitura da tecla
		tecla = le_tecla();

		if (tecla != 0 && tecla!='*' && tecla!='#') {
			contador_5s = 500;
			str[i] = tecla;		// String recebe o valor lido em cada posicao
			i++;

			lcd_putchar(tecla);	// Imprime no LCD os valores selecionados;
			_delay_ms(250);
		}
	}

	// Converte para BCD	
	dado = converter_bcd(str);	
		
	return dado;	
}

/**
 * @brief Seta dia, mes, ano, hora e minutos 
 * 
 */
void set_hora_data() {
	

	// Seta dia
	do {
		lcd_clear();
		lcd_puts("Dia:");
		dia = get_valores();
		if(timeout) return;
	} while (dia == 0 || dia > 0x31);
			
	// Seta mes
	do {
		lcd_clear();
		lcd_puts("Mes:");
		if(timeout) return;	
		mes = get_valores();				
	} while (mes == 0 || mes > 0x12);
				
	// Seta ano
	{
		lcd_clear();
		lcd_puts("Ano:");
		if(timeout) return;
		ano = get_valores();
	}
	
	// Seta horas
	do {
		lcd_clear();
		lcd_puts("Horas:");
		if(timeout) return;
		hora = get_valores();
	} while (hora > 0x23);
	 
	// Seta minutos
	do {
		lcd_clear();
		lcd_puts("Minuto:");
		if(timeout) return;
		minuto = get_valores();
	} while (minuto > 0x59);
					
	DS1307_SetTime(hora,minuto,0x30);	// envia o horario para o RTC '0'=0x30
	DS1307_SetDate(dia,mes,ano);		// envia a data para o RTC		
	TCCR0B = 0;							// desativa o timer T/C0	
}

/**
 * @brief Converte BCD para decimal
 * 
 * @param dado 
 * @return unsigned char 
 */
unsigned char bcd2dec(unsigned char dado){
	uint8_t unidades,dezenas,valor;
	unidades = dado & 0x0f;
	dezenas = ((dado & 0xf0)>>4)*10;
	valor = unidades + dezenas;
	return valor;
}

/**
 * @brief Converte todos dados lidos do RTC em BCD para decimal
 * 
 */
void converteDados(){
	dia = bcd2dec(dia);
	mes	= bcd2dec(mes);
	ano = bcd2dec(ano);
	hora = bcd2dec(hora);
	minuto = bcd2dec(minuto);
	segundo = bcd2dec(segundo);
}

/**
 * @brief Rotina de leitura do ADC
 * 
 * @param adc_input 
 * @return unsigned int 
 */
unsigned int read_adc(uint8_t adc_input)
{
	// Vref: Int.1.1V, cap. em AREF
	ADMUX  = adc_input | ((1<<REFS1)|(1<<REFS0)|(0<<ADLAR));
	_delay_us(10);	// Delay p/ estabilizar tensao em Vin
	ADCSRA |= (1<<ADSC);	// Sinal de Start Conversion (ADSC)
	while  ((ADCSRA&(1<<ADIF))==0);	// Espera final de conversao
	ADCSRA |= (1<<ADIF);	// Reseta flag de interrupcao
	return ADCW;	// Valor de retorno da conversao
}



/**
 * @brief Timer 0 para 10ms
 * 
 */
void inic_tc0(){
	// T/C0: Clock: 15,625 kHz
	// Mode: Normal to,0p=0xFF, Timer Period: 9,984 ms
	TCCR0A = 0x00;
	TCCR0B = (0<<WGM02) | (1<<CS02) | (0<<CS01) | (1<<CS00);
	TCNT0 = 100;
	// Timer/Counter 0 Interrupt(s) initialization
	TIMSK0 = (0<<OCIE0B) | (0<<OCIE0A) | (1<<TOIE0);
	ADMUX =  ((1<<REFS1)|(1<<REFS0)|(0<<ADLAR));
	ADCSRA = (1<<ADEN)|(0<<ADSC)|(0<<ADATE)|(0<<ADIF)|(0<<ADIE)|(1<<ADPS2)|(0<<ADPS1)|(0<<ADPS0);
	
	sei();
}

/**
 * @brief Rotina de interrupcao TIMER0
 * 
 */
ISR(TIMER0_OVF_vect)
{
	TCNT0 = 100; 	// Recarrega TCNT0 com valor inicial.
	contador_5s--; // Decrementa contador de 5s
	if(contador_5s == 0) { // Se tempo alcancado
		contador_5s = 500; // Reinicia contador de 5s
		timeout = 1;
	}
}

/**
 * @brief Configurar o timer1 para 1 segundo
 * 
 */
void inic_tc1()
{
	// Timer/Counter 1: Clock: 62,500 kHz
	// Mode: Normal top=0xFFFF. Timer Period: 1 s
	// Timer1 Overflow Interrupt: ON
	TCCR1A=(0<<COM1A1)|(0<<COM1A0)|(0<<COM1B1)|(0<<COM1B0)|(0<<WGM11)|(0<<WGM10);
	TCCR1B=(0<<ICNC1)|(0<<ICES1)|(0<<WGM13)|(0<<WGM12)|(1<<CS12)|(0<<CS11)|(0<<CS10);
	TCNT1 = 3036;
	// Habilita Interrupcao do Timer/Counter 1
	TIMSK1 |= (1<<TOIE1);
	sei();

}

/**
 * @brief Rotina de interrupcao do TIMER0 
 * 
 */
ISR(TIMER1_OVF_vect)
{
	//uint8_t temperatura;
	unsigned char buff_horas[5];
	unsigned char buff_data[5];
	unsigned char buff_temp[4];
	unsigned int valor;
	unsigned char buff_temp_setpoint[5];
	unsigned char buff_hr_on[9];
	unsigned char buff_hr_off[9];

	// Reinicializa o valor inicial do T/C1 para 1,0 s
	TCNT1 = 3036;
	
	valor=read_adc(0);	// Leitura do ADC
	temperatura = valor/9.3;	// Vin(V)=1,1*Valor lido/1024 => T(oC)=Vin/10mV
	sprintf(buff_temp,"%d%cC",temperatura, 223);	// Converte valor em string de ASCIIs
	lcd_gotoxy(6,0);	// Posiciona cursor do LCD
	lcd_puts(buff_temp);	// Escreve valor no LDC
	
	DS1307_GetTime(&hora,&minuto,&segundo);
	DS1307_GetDate(&dia,&mes,&ano);

	converteDados();

	sprintf(buff_data,"%02d/%02d",dia,mes);
	lcd_gotoxy(0,0);
	lcd_puts(buff_data);

	sprintf(buff_horas,"%02d:%02d",hora,minuto);
	lcd_gotoxy(0,1);
	lcd_puts(buff_horas);

	setpoint = bcd2dec(setpoint_read);
	sprintf(buff_temp_setpoint,">%d%cC",setpoint, 223);
	lcd_gotoxy(11,0);
	lcd_puts(buff_temp_setpoint);

	if (aux_5seg == 0)
	{
		if (aux_tela == 0)
		{
			hr_off  = bcd2dec(hr_off);
			min_off = bcd2dec(min_off);
			sprintf(buff_hr_off,"OFF %02d:%02d",hr_off,min_off);
			lcd_gotoxy(7,1);
			lcd_puts(buff_hr_off);
			aux_tela = 1;
			aux_5seg = 5;

		}

		else if (aux_tela == 1)
		{
			hr_on  = bcd2dec(hr_on);
			min_on = bcd2dec(min_on);
			sprintf(buff_hr_on,"ON  %02d:%02d",hr_on,min_on);
			lcd_gotoxy(7,1);
			lcd_puts(buff_hr_on);
			aux_tela = 0;
			aux_5seg = 5;

		}
		
	}
	aux_5seg--;	


}

/**
 * @brief Leitura da memoria EEPROM
 * 
 */
void read_eeprom(){
	setpoint_read=EEPROM_read(0);
	hr_on=EEPROM_read(1);
	min_on=EEPROM_read(2);
	hr_off=EEPROM_read(3);
	min_off=EEPROM_read(4);
}

/**
 * @brief Escrita na memoria da EEPROM
 * 
 */
void write_eeprom(){
	
	EEPROM_write(1,hr_on);		
	EEPROM_write(2,min_on);
	EEPROM_write(3,hr_off);
	EEPROM_write(4,min_off);
}

/**
 * @brief Ajuste de temperatura do setpoint
 * 
 */
void setpoint_temp() {				
	unsigned char str[2];

	TCCR0B=0;
	TCCR1B=0;
	inic_tc0();

	do {	// loop caso invalido					
		int i = 0;
		lcd_clear();
		lcd_puts("Temp:");				
		while (i < 2 && !timeout) {	// preencher as duas posi��es do vetor
				tecla = le_tecla();	// ler a tecla
				if (tecla != 0 && tecla!='*' && tecla!='#') {	// se leu a tecla:
					contador_5s = 500;
					str[i] = tecla;	// string recebe o valor lido em cada posi��o
					i++;
					lcd_putchar(tecla);	// mostra na tela o caracter lido
					_delay_ms(250);
				}
		}					
		if(timeout) return;
		setpoint = converter_bcd(str);	// converte para decimal
		
	} while (setpoint<=20 || setpoint> 0x28);	// se temperatura � menor que 2 ou maior que 40
	
	EEPROM_write(0,setpoint);
	inic_tc1();
}

/**
 * @brief Ajusta a hora de funcionamento
 * 
 */
void set_on_off() {

	TCCR0B = 0;
	TCCR1B = 0;
	
	// Entra Hora
	do {	// loop caso invalido		
		lcd_clear();
		lcd_puts("Hora ON:");
		
		hr_on = get_valores();
		if(timeout) return;
	} while (hr_on > 0x23);	// se hora � menor que 23:
	contador_5s = 500;
	
	// Entra Minutos
	do {	// loop caso invalido
		lcd_clear();
		lcd_puts("Min ON:");		
	
		if(timeout) return;		
		min_on = get_valores();		
	} while (min_on > 0x59);	// se min � maior que 59:
		
	// Entra Hora
	do {	// loop caso invalido
		lcd_clear();
		lcd_puts("Hora OFF:");

		if(timeout) return;
		hr_off = get_valores();
	} while (hr_off > 0x23);	// se hora � menor que 23:
	contador_5s = 500;
	
	// Entra Minutos
	do {	// loop caso invalido
		lcd_clear();
		lcd_puts("Min OFF:");

		if(timeout) return;
		min_off = get_valores();
	} while (min_off > 0x59);	// se min � maior que 59:
		
	write_eeprom();
}



/**
 * @brief Funcao main
 * 
 * @return int 
 */
int main(){
	
	uint8_t i=0;
	
	DDRC |= (1<<1);

	I2C_Inic();
	lcd4bits_inic();	// inicia o LCD
	inic_tecl_ports();	// inicia o teclado	
	DS1307_Inic();	


	lcd_puts("  Controle de  ");
	lcd_gotoxy(0,1);
	lcd_puts("  Temperatura  ");
	_delay_ms(2000);
	lcd_clear();	
	_delay_ms(250);

	set_hora_data();	// Inicializa data e hora no RTC
	lcd_clear();
	_delay_ms(250);

	lcd_puts("   BEM-VINDO  ");
		
	_delay_ms(1000);
	lcd_clear();

	timeout=0;
	inic_tc1();			// dispara o timer1 para 1 seg
	
	while(1){
		
		read_eeprom();
		contador_5s = 500;
		timeout = 0;
		tecla = le_tecla();					// ler a tecla	
		if(tecla != 0 && tecla=='*'){
			setpoint_temp();
			lcd_clear();		
			TCCR1B = 4;
		}
			
		if(tecla != 0 && tecla=='#'){
			set_on_off();
			lcd_clear();
			TCCR1B = 4;
		}

		mins_start = ((int)hr_on * 60) + (int)min_on;
		mins_stop = ((int)hr_off * 60) + (int)min_off;
		mins_now = ((int)hora * 60) + (int)minuto;
		

		if (temperatura < setpoint)
		{
			
			if ((mins_start <= mins_now) && (mins_now < mins_stop))
			{
				PORTC |= (1<<PC1);
			} 
			else 
			{
				PORTC &= ~(1<<PC1);
			}
			
		}
		
		if (temperatura > setpoint)
		{
			PORTC &= ~(1<<PC1);
		}

	}
}