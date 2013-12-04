; Example of using playroutine 2 (gamemusic)
;
; playeraddress+0 = init music (A = tunenumber starting from 0)
; playeraddress+3 = play music
; playeraddress+6 = play sound effect (A = sound effect address lowbyte,
;                                      Y = sound effect address highbyte,
;                                      X = channel index 0, 7 or 14)
;
; Remember that this player uses 2 zeropage locations, so save those
; locations before calling the player, if you need all of the ZP.
; The player doesn't depend on their contents between calls at all!



                processor 6502

                org $0800

getin           = $ffe4

start:          jsr initraster
                lda #22
                sta $d018
                lda #$00
                jsr $1000                       ;Initialize subtune 0
                ldx #$00
                lda #$20
clearscreen:    sta $0400,x
                sta $0500,x
                sta $0600,x
                sta $0700,x
                inx
                bne clearscreen
textloop:       lda text,x
                jsr convertascii
                sta $0400,x
                lda #$01
                sta $d800,x
                inx
                cpx #4*40
                bcc textloop
idle:           lda currraster
                ldx #52
                jsr printhex
                lda maxraster
                ldx #55
                jsr printhex
                jsr getin
                cmp #"A"
                bcc idle
                cmp #"D"+1
                bcs idle
playsound:      sec
                sbc #"A"                        ;Sound effect number 0-3
                tax
                lda sfxtbllo,x                  ;Address in A,Y
                ldy sfxtblhi,x
                ldx #$0e                        ;Channel index in X
                jsr $1006                       ;(0, 7 or 14)
                jmp idle

printhex:       pha
                lsr
                lsr
                lsr
                lsr
                tay
                lda hexnybble,y
                jsr convertascii
                sta $0400,x
                pla
                and #$0f
                tay
                lda hexnybble,y
                jsr convertascii
                sta $0401,x
                rts

convertascii:   cmp #$60
                bcc ca_ok
                sbc #$60
ca_ok:          rts

initraster:     sei
                lda #<raster
                sta $0314
                lda #>raster
                sta $0315
                lda #50                         ;Set low bits of raster
                sta $d012                       ;position
                lda $d011
                and #$7f                        ;Set high bit of raster
                sta $d011                       ;position (0)
                lda #$7f                        ;Set timer interrupt off
                sta $dc0d
                lda #$01                        ;Set raster interrupt on
                sta $d01a
                lda $dc0d                       ;Acknowledge timer interrupt
                cli
irwait:         lda maxraster
                beq irwait
                lda #$00
                sta maxraster
                rts

raster:         nop
                nop
                nop
                nop
                nop
                lda $d012
                sta raster_cmp+1
                inc $d020
                jsr $1003                       ;Play music and measure
                lda $d012                       ;rastertime it took to
                dec $d020                       ;execute
                sec
raster_cmp:     sbc #$00
                sta currraster
                cmp maxraster
                bcc raster_skip
                sta maxraster
raster_skip:    dec $d019
                jmp $ea31

text:           dc.b "Example 2: Game-musicroutine            "
                dc.b "Rastertime:   /                         "
                dc.b "                                        "
                dc.b "Press A-D for sound effects.            "

hexnybble:      dc.b "0123456789ABCDEF"

maxraster:      dc.b 0
currraster:     dc.b 0

sfxtbllo:       dc.b <arpeggio2                ;Our sound FX table
                dc.b <arpeggio1
                dc.b <gunshot
                dc.b <explosion

sfxtblhi:       dc.b >arpeggio2
                dc.b >arpeggio1
                dc.b >gunshot
                dc.b >explosion

arpeggio2:      include sfx_arp2.s            ;The order in memory determines
arpeggio1:      include sfx_arp1.s            ;priority when trying to override
gunshot:        include sfx_gun.s             ;a currently playing effect
explosion:      include sfx_expl.s            ;arpeggio2 = lowest pri.
                                              ;explosion = highest pri.

                org $1000

                incbin example2.bin           ;Playroutine & music data
