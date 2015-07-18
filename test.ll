; ModuleID = 'miscjunk/test.cpp'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%"struct.std::array" = type { [3 x i64] }
%"struct.std::array.0" = type { [5 x i64] }
%"struct.std::array.1" = type { [2 x i64] }

$_ZNSt5arrayImLm5EEixEm = comdat any

$_ZNSt5arrayImLm3EEixEm = comdat any

$_ZNSt14__array_traitsImLm5EE6_S_refERA5_Kmm = comdat any

$_ZNSt14__array_traitsImLm3EE6_S_refERA3_Kmm = comdat any

@_ZZ4mainE3foo = private unnamed_addr constant [12 x i32] [i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12], align 16

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
entry:
  %foo = alloca [12 x i32], align 16
  %0 = bitcast [12 x i32]* %foo to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %0, i8* bitcast ([12 x i32]* @_ZZ4mainE3foo to i8*), i64 48, i32 16, i1 false)
  ret i32 0
}

; Function Attrs: nounwind
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* nocapture, i8* nocapture readonly, i64, i32, i1) #1

; Function Attrs: nounwind uwtable
define void @_Z19compile_user_facingm(%"struct.std::array"* noalias sret %agg.result, i64 %target) #0 {
entry:
  %target.addr = alloca i64, align 8
  %x = alloca %"struct.std::array.0", align 8
  %y = alloca i64, align 8
  store i64 %target, i64* %target.addr, align 8
  %0 = bitcast %"struct.std::array"* %agg.result to i8*
  call void @llvm.memset.p0i8.i64(i8* %0, i8 0, i64 24, i32 8, i1 false)
  %1 = bitcast %"struct.std::array.0"* %x to i8*
  call void @llvm.memset.p0i8.i64(i8* %1, i8 0, i64 40, i32 8, i1 false)
  %call = call dereferenceable(8) i64* @_ZNSt5arrayImLm5EEixEm(%"struct.std::array.0"* %x, i64 1) #1
  %2 = load i64, i64* %call
  store i64 %2, i64* %y, align 8
  ret void
}

; Function Attrs: nounwind
declare void @llvm.memset.p0i8.i64(i8* nocapture, i8, i64, i32, i1) #1

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(8) i64* @_ZNSt5arrayImLm5EEixEm(%"struct.std::array.0"* %this, i64 %__n) #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::array.0"*, align 8
  %__n.addr = alloca i64, align 8
  store %"struct.std::array.0"* %this, %"struct.std::array.0"** %this.addr, align 8
  store i64 %__n, i64* %__n.addr, align 8
  %this1 = load %"struct.std::array.0"*, %"struct.std::array.0"** %this.addr
  %_M_elems = getelementptr inbounds %"struct.std::array.0", %"struct.std::array.0"* %this1, i32 0, i32 0
  %0 = load i64, i64* %__n.addr, align 8
  %call = call dereferenceable(8) i64* @_ZNSt14__array_traitsImLm5EE6_S_refERA5_Kmm([5 x i64]* dereferenceable(40) %_M_elems, i64 %0) #1
  ret i64* %call
}

; Function Attrs: nounwind uwtable
define { i64, i64 } @_Z8twoarraym(i64 %target) #0 {
entry:
  %retval = alloca %"struct.std::array.1", align 8
  %target.addr = alloca i64, align 8
  %x = alloca %"struct.std::array.0", align 8
  %y = alloca i64, align 8
  store i64 %target, i64* %target.addr, align 8
  %0 = bitcast %"struct.std::array.1"* %retval to i8*
  call void @llvm.memset.p0i8.i64(i8* %0, i8 0, i64 16, i32 8, i1 false)
  %1 = bitcast %"struct.std::array.0"* %x to i8*
  call void @llvm.memset.p0i8.i64(i8* %1, i8 0, i64 40, i32 8, i1 false)
  %call = call dereferenceable(8) i64* @_ZNSt5arrayImLm5EEixEm(%"struct.std::array.0"* %x, i64 1) #1
  %2 = load i64, i64* %call
  store i64 %2, i64* %y, align 8
  %coerce.dive = getelementptr %"struct.std::array.1", %"struct.std::array.1"* %retval, i32 0, i32 0
  %3 = bitcast [2 x i64]* %coerce.dive to { i64, i64 }*
  %4 = load { i64, i64 }, { i64, i64 }* %3, align 1
  ret { i64, i64 } %4
}

; Function Attrs: nounwind uwtable
define void @_Z10threearrayv(%"struct.std::array"* noalias sret %agg.result) #0 {
entry:
  call void @llvm.trap()
  unreachable

return:                                           ; No predecessors!
  ret void
}

; Function Attrs: noreturn nounwind
declare void @llvm.trap() #2

; Function Attrs: nounwind uwtable
define void @_Z9fourarrayv(%"struct.std::array"* noalias sret %agg.result) #0 {
entry:
  call void @llvm.trap()
  unreachable

return:                                           ; No predecessors!
  ret void
}

; Function Attrs: nounwind uwtable
define void @_Z9fivearrayv(%"struct.std::array"* noalias sret %agg.result) #0 {
entry:
  call void @llvm.trap()
  unreachable

return:                                           ; No predecessors!
  ret void
}

; Function Attrs: nounwind uwtable
define void @_Z3bahv() #0 {
entry:
  %tmp = alloca %"struct.std::array", align 8
  call void @_Z19compile_user_facingm(%"struct.std::array"* sret %tmp, i64 1)
  %call = call dereferenceable(8) i64* @_ZNSt5arrayImLm3EEixEm(%"struct.std::array"* %tmp, i64 1) #1
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(8) i64* @_ZNSt5arrayImLm3EEixEm(%"struct.std::array"* %this, i64 %__n) #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::array"*, align 8
  %__n.addr = alloca i64, align 8
  store %"struct.std::array"* %this, %"struct.std::array"** %this.addr, align 8
  store i64 %__n, i64* %__n.addr, align 8
  %this1 = load %"struct.std::array"*, %"struct.std::array"** %this.addr
  %_M_elems = getelementptr inbounds %"struct.std::array", %"struct.std::array"* %this1, i32 0, i32 0
  %0 = load i64, i64* %__n.addr, align 8
  %call = call dereferenceable(8) i64* @_ZNSt14__array_traitsImLm3EE6_S_refERA3_Kmm([3 x i64]* dereferenceable(24) %_M_elems, i64 %0) #1
  ret i64* %call
}

; Function Attrs: nounwind uwtable
define void @_Z4testv() #0 {
entry:
  %x = alloca i32, align 4
  %y = alloca i32, align 4
  store i32 0, i32* %x, align 4
  store i32 14, i32* %y, align 4
  %0 = load i32, i32* %x, align 4
  %add = add nsw i32 %0, 4
  store i32 %add, i32* %y, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.body, %entry
  %1 = load i32, i32* %x, align 4
  %tobool = icmp ne i32 %1, 0
  br i1 %tobool, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %2 = load i32, i32* %y, align 4
  %add1 = add nsw i32 %2, 20
  store i32 %add1, i32* %x, align 4
  %3 = load i32, i32* %x, align 4
  %mul = mul nsw i32 %3, 2
  store i32 %mul, i32* %y, align 4
  br label %for.cond

for.end:                                          ; preds = %for.cond
  br label %a

a:                                                ; preds = %for.end
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(8) i64* @_ZNSt14__array_traitsImLm5EE6_S_refERA5_Kmm([5 x i64]* dereferenceable(40) %__t, i64 %__n) #0 comdat align 2 {
entry:
  %__t.addr = alloca [5 x i64]*, align 8
  %__n.addr = alloca i64, align 8
  store [5 x i64]* %__t, [5 x i64]** %__t.addr, align 8
  store i64 %__n, i64* %__n.addr, align 8
  %0 = load i64, i64* %__n.addr, align 8
  %1 = load [5 x i64]*, [5 x i64]** %__t.addr, align 8
  %arrayidx = getelementptr inbounds [5 x i64], [5 x i64]* %1, i32 0, i64 %0
  ret i64* %arrayidx
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(8) i64* @_ZNSt14__array_traitsImLm3EE6_S_refERA3_Kmm([3 x i64]* dereferenceable(24) %__t, i64 %__n) #0 comdat align 2 {
entry:
  %__t.addr = alloca [3 x i64]*, align 8
  %__n.addr = alloca i64, align 8
  store [3 x i64]* %__t, [3 x i64]** %__t.addr, align 8
  store i64 %__n, i64* %__n.addr, align 8
  %0 = load i64, i64* %__n.addr, align 8
  %1 = load [3 x i64]*, [3 x i64]** %__t.addr, align 8
  %arrayidx = getelementptr inbounds [3 x i64], [3 x i64]* %1, i32 0, i64 %0
  ret i64* %arrayidx
}

attributes #0 = { nounwind uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind }
attributes #2 = { noreturn nounwind }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.7.0 (trunk 241399)"}
