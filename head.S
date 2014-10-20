    
  .syntax unified

  .section  .text.head

.global vector_table
vector_table:
    .word _stext
    .rept 85-2
    .word entry
    .endr
        
.global entry
.type entry,%function
entry:
    bl  main
    bx  lr
