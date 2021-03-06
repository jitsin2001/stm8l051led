;--------------------------------------------------------
;       STM8EF for STM8S (Value line and Access Line devices)
;       Simulated serial I/O - two GPIOs - Full Duplex
;
;       NOTE: this variant supersedes inc/sser_fdx.inc
;             - look for "inverted TX"
;--------------------------------------------------------

        .ifne   HAS_TXSIM ;+ HAS_RXSIM
        RamByte TIM4RCNT        ; TIM4 RX interrupt counter
        RamByte TIM4TCNT        ; TIM4 TX interrupt counter
        RamByte TIM4TXREG       ; TIM4 TX transmit buffer and shift register
        RamByte TIM4RXREG       ; TIM4 RX shift register
        RamByte TIM4RXBUF       ; TIM4 RX receive buffer
        RamByte TIM4RXFLAG      ; TIM4 RX receive flag
        .endif

        .ifne   SERPRIOTIM
        RamWord TIMSERIAL       ; "TIMSERIAL" Timer: use PSIM,PNRX for serial while not 0
        RamByte TIMRELOAD       ; Reload value for TIMSERIAL, set by receive interrupt, reset by timer
        .endif

        .ifne   HAS_RXSIM
;       ?RXP     ( -- c T | F )  ( TOS STM8: -- Y,Z,N )
;       Return char from a simulated serial interface and true, or false.

        .ifeq   BAREBONES
        .ifne   HAS_RXUART
        HEADER  QRXP "?RXP"
        .else
        HEADER  QRX "?RX"
        .endif
        .endif
        .ifeq   HAS_RXUART
QRX:
        .endif
QRXP:
        CLR     A
        EXG     A,TIM4RXFLAG    ; read and consume char
        TNZ     A
        JRNE    0$
        JP      ZERO
0$:
        LD      A,TIM4RXBUF     ; read char
        CALL    ASTOR
        JP      MONE
        .endif

        .ifne   HAS_TXSIM
;       TXP!     ( c -- )
;       Send character c to a simulated serial interface.

        .ifeq   BAREBONES
        .ifne   HAS_TXUART
        HEADER  TXPSTOR "TXP!"
        .else
        HEADER  TXSTOR "TX!"
        .endif
        .endif

        .ifeq   HAS_TXUART
TXSTOR:
        .endif
TXPSTOR:
        .ifne   SERPRIOTIM
        MOV     TIMSERIAL,TIMRELOAD ; load MSB msec ticker
        .endif

        INCW    X
        LD      A,(X)
        INCW    X

1$:     TNZ     TIM4TCNT
        JRNE    1$              ; wait for TIM4 TX complete

        LD      TIM4TXREG,A     ; char to TXSIM output register
        MOV     TIM4TCNT,#10    ; init next transfer
        TNZ     TIM4RCNT        ; test if RX already uses TIM4
        JRNE    2$
        CLR     TIM4_CNTR       ; reset TIM4, trigger update interrupt
        BSET    TIM4_IER,#0     ; enable TIM4 interrupt
2$:
        RET
        .endif

;       RxD through GPIO start-bit interrupt handler


        .ifne   HAS_RXSIM
        .globl _EXTI0_IRQHandler
        .globl _EXTI1_IRQHandler
        .globl _EXTI2_IRQHandler
        .globl _EXTI3_IRQHandler
        .globl _EXTI4_IRQHandler
        .globl _EXTI5_IRQHandler
        .globl _EXTI6_IRQHandler
        .globl _EXTI7_IRQHandler

_EXTI0_IRQHandler:
_EXTI1_IRQHandler:
_EXTI2_IRQHandler:
_EXTI3_IRQHandler:
_EXTI4_IRQHandler:
_EXTI5_IRQHandler:
_EXTI6_IRQHandler:
_EXTI7_IRQHandler:

        BRES    PSIM+CR2,#PNRX  ; disable PNRX external interrupt

        .ifeq   (FAMILY - STM8L)
        BSET    EXTI_SR1,#PNRX  ; STM8L: clear interrupt
        .endif

        .ifne   SERPRIOTIM
        MOV     TIMRELOAD,#(SERPRIOTIM/256) ; load MSB msec reload
        MOV     TIMSERIAL,TIMRELOAD ; load MSB msec ticker
        .endif

        MOV     TIM4RCNT,#9     ; set sequence counter for RX

        ; Set-up Rx sampling at quarter bit time (compromise with TX)
        MOV     TIM4_CNTR,#(CTIM4ARR/2)
        BRES    TIM4_SR,#0      ; clear TIM4 UIF
        BSET    TIM4_IER,#0     ; enable TIM4 interrupt
        IRET
        .endif

        RamWord TSTAMP          ; "TSTAMP" TIM2 counter at end of receive

        .globl _TIM4_IRQHandler
_TIM4_IRQHandler:
        ; TIM4 interrupt handler for software Rx/Tx
        BRES    TIM4_SR,#0      ; clear TIM4 UIF

        LD      A,TIM4RCNT      ; test receive step counter
        JREQ    TIM4_TESTTRANS  ; nothing to do - check for transmit

        ; Receive a bit
        BTJT    PSIM+IDR,#PNRX,1$ ; dummy branch, copy GPIO to CF
1$:     RRC     TIM4RXREG
        DEC     TIM4RCNT
        JRNE    TIM4_TESTTRANS

        ; Receive sequence complete
        MOV     TIM4RXBUF,TIM4RXREG ; save result
        CLR     TIM4RXFLAG
        CPL     TIM4RXFLAG

        ; Record timestamp for synchronization with sender cadence
        MOV     TSTAMP  ,TIM2_CNTRH
        MOV     TSTAMP+1,TIM2_CNTRL

        BSET    PSIM+CR2,#PNRX  ; enable PNRX external interrupt
        ; fall through

TIM4_TESTTRANS:
        LD      A,TIM4TCNT      ; test transmit step counter
        JREQ    TIM4_TESTOFF
        ; fall through

TIM4_TRANS:
        CP      A,#10           ; startbit? (also sets CF)
        JRNE    TIM4_TRANSSER
        JRA     TIM4_TRANSBIT        ; emit start bit (CF=0 from "CP A")

        TIM4_TRANSSER:
        RRC     TIM4TXREG       ; get data bit, shift in stop bit (CF=1 from "CP A")
        ; fall through

TIM4_TRANSBIT:
        CCF                     ; inverted TX
        BCCM    PSIM+ODR,#PNTX  ; Set GPIO to CF
        DEC     TIM4TCNT        ; next TXD TIM4 state
        JRNE    TIM4_END        ; not complete unless TIM4TCNT is zero
        ; fall through

TIM4_TESTOFF:
        LD      A,TIM4RCNT
        JRNE    TIM4_END
        BRES    PSIM+ODR,#PNTX  ; inverted TX - set TX GPIO to STOP
        BRES    TIM4_IER,#0     ; disable TIM4 interrupt
        ; fall through

TIM4_END:
        IRET

