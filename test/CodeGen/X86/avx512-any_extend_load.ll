; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gn -mcpu=knl | FileCheck %s --check-prefix=ALL --check-prefix=KNL
; RUN: llc < %s -mtriple=x86_64-unknown-linux-gn -mcpu=skx | FileCheck %s --check-prefix=ALL --check-prefix=SKX


define void @any_extend_load_v8i64(<8 x i8> * %ptr) {
; KNL-LABEL: any_extend_load_v8i64:
; KNL:       # %bb.0:
; KNL-NEXT:    vpmovzxbq {{.*#+}} zmm0 = mem[0],zero,zero,zero,zero,zero,zero,zero,mem[1],zero,zero,zero,zero,zero,zero,zero,mem[2],zero,zero,zero,zero,zero,zero,zero,mem[3],zero,zero,zero,zero,zero,zero,zero,mem[4],zero,zero,zero,zero,zero,zero,zero,mem[5],zero,zero,zero,zero,zero,zero,zero,mem[6],zero,zero,zero,zero,zero,zero,zero,mem[7],zero,zero,zero,zero,zero,zero,zero
; KNL-NEXT:    vpaddq {{.*}}(%rip){1to8}, %zmm0, %zmm0
; KNL-NEXT:    vpmovqb %zmm0, (%rdi)
; KNL-NEXT:    retq
;
; SKX-LABEL: any_extend_load_v8i64:
; SKX:       # %bb.0:
; SKX-NEXT:    vpmovzxbq {{.*#+}} zmm0 = mem[0],zero,zero,zero,zero,zero,zero,zero,mem[1],zero,zero,zero,zero,zero,zero,zero,mem[2],zero,zero,zero,zero,zero,zero,zero,mem[3],zero,zero,zero,zero,zero,zero,zero,mem[4],zero,zero,zero,zero,zero,zero,zero,mem[5],zero,zero,zero,zero,zero,zero,zero,mem[6],zero,zero,zero,zero,zero,zero,zero,mem[7],zero,zero,zero,zero,zero,zero,zero
; SKX-NEXT:    vpaddq {{.*}}(%rip){1to8}, %zmm0, %zmm0
; SKX-NEXT:    vpmovqb %zmm0, (%rdi)
; SKX-NEXT:    vzeroupper
; SKX-NEXT:    retq
  %wide.load = load <8 x i8>, <8 x i8>* %ptr, align 1
  %1 = zext <8 x i8> %wide.load to <8 x i64>
  %2 = add nuw nsw <8 x i64> %1, <i64 4, i64 4, i64 4, i64 4, i64 4, i64 4, i64 4, i64 4>
  %3 = xor <8 x i64> %2, zeroinitializer
  %4 = trunc <8 x i64> %3 to <8 x i8>
  store <8 x i8> %4, <8 x i8>* %ptr, align 1
  ret void
}

define void @any_extend_load_v8i32(<8 x i8> * %ptr) {
; KNL-LABEL: any_extend_load_v8i32:
; KNL:       # %bb.0:
; KNL-NEXT:    vpmovzxbw {{.*#+}} xmm0 = mem[0],zero,mem[1],zero,mem[2],zero,mem[3],zero,mem[4],zero,mem[5],zero,mem[6],zero,mem[7],zero
; KNL-NEXT:    vpaddw {{.*}}(%rip), %xmm0, %xmm0
; KNL-NEXT:    vpshufb {{.*#+}} xmm0 = xmm0[0,2,4,6,8,10,12,14,u,u,u,u,u,u,u,u]
; KNL-NEXT:    vmovq %xmm0, (%rdi)
; KNL-NEXT:    retq
;
; SKX-LABEL: any_extend_load_v8i32:
; SKX:       # %bb.0:
; SKX-NEXT:    vpmovzxbd {{.*#+}} ymm0 = mem[0],zero,zero,zero,mem[1],zero,zero,zero,mem[2],zero,zero,zero,mem[3],zero,zero,zero,mem[4],zero,zero,zero,mem[5],zero,zero,zero,mem[6],zero,zero,zero,mem[7],zero,zero,zero
; SKX-NEXT:    vpaddd {{.*}}(%rip){1to8}, %ymm0, %ymm0
; SKX-NEXT:    vpmovdb %ymm0, (%rdi)
; SKX-NEXT:    vzeroupper
; SKX-NEXT:    retq
  %wide.load = load <8 x i8>, <8 x i8>* %ptr, align 1
  %1 = zext <8 x i8> %wide.load to <8 x i32>
  %2 = add nuw nsw <8 x i32> %1, <i32 4, i32 4, i32 4, i32 4, i32 4, i32 4, i32 4, i32 4>
  %3 = xor <8 x i32> %2, zeroinitializer
  %4 = trunc <8 x i32> %3 to <8 x i8>
  store <8 x i8> %4, <8 x i8>* %ptr, align 1
  ret void
}

define void @any_extend_load_v8i16(<8 x i8> * %ptr) {
; KNL-LABEL: any_extend_load_v8i16:
; KNL:       # %bb.0:
; KNL-NEXT:    vpmovzxbw {{.*#+}} xmm0 = mem[0],zero,mem[1],zero,mem[2],zero,mem[3],zero,mem[4],zero,mem[5],zero,mem[6],zero,mem[7],zero
; KNL-NEXT:    vpaddb {{.*}}(%rip), %xmm0, %xmm0
; KNL-NEXT:    vpackuswb %xmm0, %xmm0, %xmm0
; KNL-NEXT:    vmovq %xmm0, (%rdi)
; KNL-NEXT:    retq
;
; SKX-LABEL: any_extend_load_v8i16:
; SKX:       # %bb.0:
; SKX-NEXT:    vpmovzxbw {{.*#+}} xmm0 = mem[0],zero,mem[1],zero,mem[2],zero,mem[3],zero,mem[4],zero,mem[5],zero,mem[6],zero,mem[7],zero
; SKX-NEXT:    vpaddw {{.*}}(%rip), %xmm0, %xmm0
; SKX-NEXT:    vpmovwb %xmm0, (%rdi)
; SKX-NEXT:    retq
  %wide.load = load <8 x i8>, <8 x i8>* %ptr, align 1
  %1 = zext <8 x i8> %wide.load to <8 x i16>
  %2 = add nuw nsw <8 x i16> %1, <i16 4, i16 4, i16 4, i16 4, i16 4, i16 4, i16 4, i16 4>
  %3 = xor <8 x i16> %2, zeroinitializer
  %4 = trunc <8 x i16> %3 to <8 x i8>
  store <8 x i8> %4, <8 x i8>* %ptr, align 1
  ret void
}
