#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "emulator.h"

#define XSTR(x) STR(x)		//can be used for MAX_ARG_LEN in sscanf
#define STR(x) #x

#define ADDR_TEXT    0x00400000 //where the .text area starts in which the program lives
#define TEXT_POS(a)  ((a==ADDR_TEXT)?(0):(a - ADDR_TEXT)/4) //can be used to access text[]
#define ADDR_POS(j)  (j*4 + ADDR_TEXT)                      //convert text index to address


const char *register_str[] = {"$zero",
                              "$at", "$v0", "$v1",
                              "$a0", "$a1", "$a2", "$a3",
                              "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
                              "$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
                              "$t8", "$t9",
                              "$k0", "$k1",
                              "$gp",
                              "$sp", "$fp", "$ra"};

/* Space for the assembler program */
char prog[MAX_PROG_LEN][MAX_LINE_LEN];
int prog_len = 0;

/* Elements for running the emulator */
unsigned int registers[MAX_REGISTER] = {0}; // the registers
unsigned int pc = 0;                        // the program counter
unsigned int text[MAX_PROG_LEN] = {0}; // the text memory with our instructions

/* function to create bytecode for instruction nop
   conversion result is passed in bytecode
   function always returns 0 (conversion OK) */
typedef int (*opcode_function)(unsigned int, unsigned int*, char*, char*, char*, char*);

int add_imi(unsigned int *bytecode, int imi){
	if (imi<-32768 || imi>32767) return (-1);
	*bytecode|= (0xFFFF & imi);
	return(0);
}

int add_sht(unsigned int *bytecode, int sht){
	if (sht<0 || sht>31) return(-1);
	*bytecode|= (0x1F & sht) << 6;
	return(0);
}

int add_reg(unsigned int *bytecode, char *reg, int pos){
	int i;
	for(i=0;i<MAX_REGISTER;i++){
		if(!strcmp(reg,register_str[i])){
		*bytecode |= (i << pos);
			return(0);
		}
	}
	return(-1);
}

int add_addr(unsigned int *bytecode, int addr){
    *bytecode |= ((addr>>2) & 0x3FFFFF);
    return 0;
}

int add_lbl(unsigned int offset, unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_imi( bytecode, j-(offset+1)) );
		j++;
	}
	return (-1);
}

int add_text_addr(unsigned int *bytecode, char *label){
	char l[MAX_ARG_LEN+1];
	int j=0;
	while(j<prog_len){
		memset(l,0,MAX_ARG_LEN+1);
		sscanf(&prog[j][0],"%" XSTR(MAX_ARG_LEN) "[^:]:", l);
		if (label!=NULL && !strcmp(l, label)) return(add_addr( bytecode, ADDR_POS(j)));
		j++;
	}
	return (-1);
}

int opcode_nop(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0;
	return (0);
}

int opcode_add(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20; 				// op,shamt,funct
	if (add_reg(bytecode,arg1,11)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_reg(bytecode,arg3,16)<0) return (-1);	// source2 register
	return (0);
}

int opcode_addi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x20000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_andi(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x30000000; 				// op
	if (add_reg(bytecode,arg1,16)<0) return (-1); 	// destination register
	if (add_reg(bytecode,arg2,21)<0) return (-1);	// source1 register
	if (add_imi(bytecode,atoi(arg3))) return (-1);	// constant
	return (0);
}

int opcode_blez(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x18000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// register1
	if (add_lbl(offset,bytecode,arg2)) return (-1); // jump
	return (0);
}

int opcode_bne(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x14000000; 				// op
	if (add_reg(bytecode,arg1,21)<0) return (-1); 	// register1
	if (add_reg(bytecode,arg2,16)<0) return (-1);	// register2
	if (add_lbl(offset,bytecode,arg3)) return (-1); // jump
	return (0);
}

int opcode_srl(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x2; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);   // destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1);   // source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_sll(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0; 					// op
	if (add_reg(bytecode,arg1,11)<0) return (-1);	// destination register
	if (add_reg(bytecode,arg2,16)<0) return (-1); 	// source1 register
	if (add_sht(bytecode,atoi(arg3))<0) return (-1);// shift
	return(0);
}

int opcode_jr(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x8; 					// op
	if (add_reg(bytecode,arg1,21)<0) return (-1);	// source register
	return(0);
}

int opcode_jal(unsigned int offset, unsigned int *bytecode, char *opcode, char *arg1, char *arg2, char *arg3 ){
	*bytecode=0x0C000000; 					// op
	if (add_text_addr(bytecode, arg1)<0) return (-1);// find and add address
	return(0);
}

const char *opcode_str[] = {"nop", "add", "addi", "andi", "blez", "bne", "srl", "sll", "jal", "jr"};
opcode_function opcode_func[] = {&opcode_nop, &opcode_add, &opcode_addi, &opcode_andi, &opcode_blez, &opcode_bne, &opcode_srl, &opcode_sll, &opcode_jal, &opcode_jr};

/* a function to print the state of the machine */
int print_registers() {
  int i;
  printf("registers:\n");
  for (i = 0; i < MAX_REGISTER; i++) {
    printf(" %d: %d\n", i, registers[i]);
  }
  printf(" Program Counter: 0x%08x\n", pc);
  return (0);
}


/* function to execute bytecode */
int exec_bytecode() {
  printf("EXECUTING PROGRAM ...\n");
  pc = ADDR_TEXT; // set program counter to the start of our program
  
  //makes all of the variables for the code
int temp;
int temp2;
int temp3;
unsigned int bytecode;
unsigned int functcode;
unsigned int tempconst;
//maks the bytecode so i can compare
     //sets the bytecode functcode to do operations
  bytecode = text[TEXT_POS(pc)]&0xFC000000;
  functcode = text[TEXT_POS(pc)]<<26;
  functcode = functcode>>26;
//loops while there is a bytecode in teh file

  while( text[TEXT_POS(pc)] != 0x00000000){
  
  
    bytecode = text[TEXT_POS(pc)]&0xFC000000;
    functcode = text[TEXT_POS(pc)]<<26;
    functcode = functcode>>26;

    //addi
    // does the addi operation 
   if(bytecode == 0x20000000)
   {
     // sets the registers to temerery variables until they are set to the registers
      temp = text[TEXT_POS(pc)]>>21;
      temp = temp&0x1f;
      temp2 = text[TEXT_POS(pc)]>>16;
      temp2 = temp2&0x1f;
      tempconst = text[TEXT_POS(pc)]&0xff;
      registers[temp2] = registers[temp] + tempconst;
   }
   
   //andi
   //does the andi operation
   if (bytecode == 0x30000000)
   {
     temp = text[TEXT_POS(pc)]>>21;
     temp = temp&0x1f;
     temp2 = text[TEXT_POS(pc)]>>16;
     temp2 =temp2&0x1f;
     tempconst = text[TEXT_POS(pc)]&0xff;
     registers[temp2] = tempconst << registers[temp];
     
   }
   //blez
   //does the branch if less than or equal to
   if (bytecode == 0x18000000)
   {
     temp = text[TEXT_POS(pc)]>>21;
     temp = temp&0x1f;
     temp2 = text[TEXT_POS(pc)]>>16;
     temp2 =temp2&0x1f;
     tempconst = text[TEXT_POS(pc)]&0xff;
     if (temp2 <= temp){
        pc = (0x400000+(tempconst));
     }
   }
   //bne
   //does the branch if not equal to 
   if (bytecode == 0x14000000)
   {
     temp = text[TEXT_POS(pc)]>>21;
     temp = temp&0x1f;
     temp2 = text[TEXT_POS(pc)]>>16;
     temp2 =temp2&0x1f;
     tempconst = text[TEXT_POS(pc)]&0xff;
     if (registers[temp] != registers[temp2])
      {
        pc = (0x400000+ tempconst);
      }
   }

   
   if (bytecode == 0x0)
   {
     if (functcode == 0x20)
     {
       //add
        temp = text[TEXT_POS(pc)]>>21;
        temp = temp&0x1f;
        temp2 = text[TEXT_POS(pc)]>>16;
        temp2 = temp2&0x1f;
        temp3 = text[TEXT_POS(pc)]>>11;
        temp3 = temp3&0x1f;
        registers[temp3] = registers[temp2] + registers[temp];
     }
    //srl
     if (functcode == 0x02)
      {

        temp = text[TEXT_POS(pc)]>>16;
        temp = temp&0x1f;
        temp2 = text[TEXT_POS(pc)]>>11;
        temp2 = temp2&0x1f;
        temp3 = text[TEXT_POS(pc)]>>6;
        temp3 = temp3&0x1f;
        registers[temp3] = registers[temp2] >> 1;
      }
     //sll
     if (functcode == 0x00)
     {
        temp = text[TEXT_POS(pc)]>>16;
        temp = temp&0x1f;
        temp2 = text[TEXT_POS(pc)]>>11;
        temp2 = temp2&0x1f;
        temp3 = text[TEXT_POS(pc)]>>6;
        temp3 = temp3&0x1f;
        registers[temp3] = registers[temp2] << 1;
       
     }
     
   }
  pc += 4;
  
 }


  // print_registers(); 
  
  for (int i=0; i<32 ; i++)
  {
    printf("%x\n", text[i]);
  }

  printf("... DONE!\n");
  return (0);
  

}

/* function to create bytecode */
int make_bytecode() {
  unsigned int
      bytecode; // holds the bytecode for each converted program instruction
  int i, j = 0; prog_len;   // instruction counter (equivalent to program line)

  char label[MAX_ARG_LEN + 1];
  char opcode[MAX_ARG_LEN + 1];
  char arg1[MAX_ARG_LEN + 1];
  char arg2[MAX_ARG_LEN + 1];
  char arg3[MAX_ARG_LEN + 1];

  printf("ASSEMBLING PROGRAM ...\n");
  while (j < prog_len) {
    memset(label, 0, sizeof(label));
    memset(opcode, 0, sizeof(opcode));
    memset(arg1, 0, sizeof(arg1));
    memset(arg2, 0, sizeof(arg2));
    memset(arg3, 0, sizeof(arg3));

    bytecode = 0;

    if (strchr(&prog[j][0], ':')) { // check if the line contains a label
      if (sscanf(
              &prog[j][0],
              "%" XSTR(MAX_ARG_LEN) "[^:]: %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                  MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
              label, opcode, arg1, arg2,
              arg3) < 2) { // parse the line with label
        printf("parse error line %d\n", j);
        return (-1);
      }
    } else {
      if (sscanf(&prog[j][0],
                 "%" XSTR(MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s %" XSTR(
                     MAX_ARG_LEN) "s %" XSTR(MAX_ARG_LEN) "s",
                 opcode, arg1, arg2,
                 arg3) < 1) { // parse the line without label
        printf("parse error line %d\n", j);
        return (-1);
      }
    }

    for (i=0; i<MAX_OPCODE; i++){
        if (!strcmp(opcode, opcode_str[i]) && ((*opcode_func[i]) != NULL))
        {
            if ((*opcode_func[i])(j, &bytecode, opcode, arg1, arg2, arg3) < 0)
            {
                printf("ERROR: line %d opcode error (assembly: %s %s %s %s)\n", j, opcode, arg1, arg2, arg3);
                return (-1);
            }
            else
            {
                printf("0x%08x 0x%08x\n", ADDR_TEXT + 4 * j, bytecode);
                text[j] = bytecode;
                break;
            }
        }
        if (i == (MAX_OPCODE - 1))
        {
            printf("ERROR: line %d unknown opcode\n", j);
            return (-1);
        }
    }

    j++;
  }
  printf("... DONE!\n");
  return (0);
}

/* loading the program into memory */
int load_program(char *filename) {
  int j = 0;
  FILE *f;

  printf("LOADING PROGRAM %s ...\n", filename);

  f = fopen(filename, "r");
  if (f == NULL) {
      printf("ERROR: Cannot open program %s...\n", filename);
      return -1;
  }
  while (fgets(&prog[prog_len][0], MAX_LINE_LEN, f) != NULL) {
    prog_len++;
  }

  printf("PROGRAM:\n");
  for (j = 0; j < prog_len; j++) {
    printf("%d: %s", j, &prog[j][0]);
  }

  return (0);
}
