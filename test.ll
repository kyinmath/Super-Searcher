; ModuleID = 'miscjunk/test.cpp'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%"struct.std::array" = type { [5 x i64] }

@_ZZ4mainE3foo = private unnamed_addr constant [12 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12], align 16

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
  %foo = alloca [12 x i32], align 16
  %1 = bitcast [12 x i32]* %foo to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %1, i8* bitcast ([12 x i32]* @_ZZ4mainE3foo to i8*), i64 48, i32 16, i1 false)
  ret i32 0
}

; Function Attrs: nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture, i8* nocapture readonly, i64, i32, i1) #1

; Function Attrs: nounwind uwtable
define void @_Z19compile_user_facingm(%"struct.std::array"* noalias sret %agg.result, i64 %target) #0 {
  %1 = alloca i64, align 8
  %y = alloca i64, align 8
  store i64 %target, i64* %1, align 8
  %2 = bitcast %"struct.std::array"* %agg.result to i8*
  call void @llvm.memset.p0i8.i64(i8* %2, i8 0, i64 40, i32 8, i1 false)
  %3 = call dereferenceable(8) i64* @_ZNSt5arrayImLm5EEixEm(%"struct.std::array"* %agg.result, i64 1) #1
  %4 = load i64* %3
  store i64 %4, i64* %y, align 8
  ret void
}

; Function Attrs: nounwind
declare void @llvm.memset.p0i8.i64(i8* nocapture, i8, i64, i32, i1) #1

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(8) i64* @_ZNSt5arrayImLm5EEixEm(%"struct.std::array"* %this, i64 %__n) #0 align 2 {
  %1 = alloca %"struct.std::array"*, align 8
  %2 = alloca i64, align 8
  store %"struct.std::array"* %this, %"struct.std::array"** %1, align 8
  store i64 %__n, i64* %2, align 8
  %3 = load %"struct.std::array"** %1
  %4 = getelementptr inbounds %"struct.std::array"* %3, i32 0, i32 0
  %5 = load i64* %2, align 8
  %6 = call dereferenceable(8) i64* @_ZNSt14__array_traitsImLm5EE6_S_refERA5_Kmm([5 x i64]* dereferenceable(40) %4, i64 %5) #1
  ret i64* %6
}

; Function Attrs: nounwind uwtable
define void @_Z3bahv() #0 {
  %1 = alloca %"struct.std::array", align 8
  call void @_Z19compile_user_facingm(%"struct.std::array"* sret %1, i64 1)
  %2 = call dereferenceable(8) i64* @_ZNSt5arrayImLm5EEixEm(%"struct.std::array"* %1, i64 1) #1
  ret void
}

; Function Attrs: nounwind uwtable
define void @_Z4testv() #0 {
  %x = alloca i32, align 4
  %y = alloca i32, align 4
  store i32 0, i32* %x, align 4
  store i32 14, i32* %y, align 4
  %1 = load i32* %x, align 4
  %2 = add nsw i32 %1, 4
  store i32 %2, i32* %y, align 4
  br label %3

; <label>:3                                       ; preds = %6, %0
  %4 = load i32* %x, align 4
  %5 = icmp ne i32 %4, 0
  br i1 %5, label %6, label %11

; <label>:6                                       ; preds = %3
  %7 = load i32* %y, align 4
  %8 = add nsw i32 %7, 20
  store i32 %8, i32* %x, align 4
  %9 = load i32* %x, align 4
  %10 = mul nsw i32 %9, 2
  store i32 %10, i32* %y, align 4
  br label %3

; <label>:11                                      ; preds = %3
  br label %12

; <label>:12                                      ; preds = %11
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(8) i64* @_ZNSt14__array_traitsImLm5EE6_S_refERA5_Kmm([5 x i64]* dereferenceable(40) %__t, i64 %__n) #0 align 2 {
  %1 = alloca [5 x i64]*, align 8
  %2 = alloca i64, align 8
  store [5 x i64]* %__t, [5 x i64]** %1, align 8
  store i64 %__n, i64* %2, align 8
  %3 = load i64* %2, align 8
  %4 = load [5 x i64]** %1, align 8
  %5 = getelementptr inbounds [5 x i64]* %4, i32 0, i64 %3
  ret i64* %5
}

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind }

!llvm.ident = !{!0}

!0 = !{!"Ubuntu clang version 3.6.2-svn238746-1~exp1 (branches/release_36) (based on LLVM 3.6.2)"}