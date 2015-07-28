; ModuleID = 'miscjunk/test.cpp'
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%"struct.std::array" = type { [3 x i64] }
%"struct.std::array.0" = type { [5 x i64] }
%"struct.std::array.1" = type { [2 x i64] }
%"struct.std::pair" = type { i64, i64 }
%"struct.std::pair.2" = type { i32, i32 }
%"class.std::tuple" = type { %"struct.std::_Tuple_impl" }
%"struct.std::_Tuple_impl" = type { %"struct.std::_Tuple_impl.3", %"struct.std::_Head_base.5" }
%"struct.std::_Tuple_impl.3" = type { %"struct.std::_Head_base" }
%"struct.std::_Head_base" = type { i64 }
%"struct.std::_Head_base.5" = type { i64 }
%"class.std::tuple.6" = type { %"struct.std::_Tuple_impl.7" }
%"struct.std::_Tuple_impl.7" = type { %"struct.std::_Tuple_impl.8", %"struct.std::_Head_base.10" }
%"struct.std::_Tuple_impl.8" = type { %"struct.std::_Head_base.9" }
%"struct.std::_Head_base.9" = type { i32 }
%"struct.std::_Head_base.10" = type { i32 }
%"class.std::tuple.11" = type { %"struct.std::_Tuple_impl.12" }
%"struct.std::_Tuple_impl.12" = type { %"struct.std::_Tuple_impl.13", %"struct.std::_Head_base.5" }
%"struct.std::_Tuple_impl.13" = type { %"struct.std::_Tuple_impl.14", %"struct.std::_Head_base" }
%"struct.std::_Tuple_impl.14" = type { %"struct.std::_Head_base.17" }
%"struct.std::_Head_base.17" = type { i64 }
%"class.std::tuple.18" = type { %"struct.std::_Tuple_impl.19" }
%"struct.std::_Tuple_impl.19" = type { %"struct.std::_Tuple_impl.20", %"struct.std::_Head_base.10" }
%"struct.std::_Tuple_impl.20" = type { %"struct.std::_Tuple_impl.21", %"struct.std::_Head_base.9" }
%"struct.std::_Tuple_impl.21" = type { %"struct.std::_Head_base.22" }
%"struct.std::_Head_base.22" = type { i32 }
%"struct.std::_Tuple_impl.4" = type { i8 }
%"struct.std::_Tuple_impl.15" = type { i8 }

$_ZNSt5arrayImLm5EEixEm = comdat any

$_ZNSt5arrayImLm3EEixEm = comdat any

$_ZSt9make_pairIiiESt4pairINSt17__decay_and_stripIT_E6__typeENS1_IT0_E6__typeEEOS2_OS5_ = comdat any

$_ZNSt4pairImmEC2IiivEEOS_IT_T0_E = comdat any

$_ZSt10make_tupleIJiiEESt5tupleIJDpNSt17__decay_and_stripIT_E6__typeEEEDpOS2_ = comdat any

$_ZNSt5tupleIJmmEEC2IiivEEOS_IJT_T0_EE = comdat any

$_ZSt10make_tupleIJiiiEESt5tupleIJDpNSt17__decay_and_stripIT_E6__typeEEEDpOS2_ = comdat any

$_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE = comdat any

$_ZNSt4pairIiiEC2IiivEEOT_OT0_ = comdat any

$_ZNSt5tupleIJiiEEC2IiivEEOT_OT0_ = comdat any

$_ZNSt11_Tuple_implILm0EJiiEEC2IiJiEvEEOT_DpOT0_ = comdat any

$_ZNSt11_Tuple_implILm1EJiEEC2IiJEvEEOT_DpOT0_ = comdat any

$_ZNSt10_Head_baseILm0EiLb0EEC2IiEEOT_ = comdat any

$_ZNSt10_Head_baseILm1EiLb0EEC2IiEEOT_ = comdat any

$_ZNSt11_Tuple_implILm0EJmmEEC2IiJiEEEOS_ILm0EJT_DpT0_EE = comdat any

$_ZSt4moveIRSt11_Tuple_implILm1EJiEEEONSt16remove_referenceIT_E4typeEOS4_ = comdat any

$_ZNSt11_Tuple_implILm0EJiiEE7_M_tailERS0_ = comdat any

$_ZNSt11_Tuple_implILm1EJmEEC2IiJEEEOS_ILm1EJT_DpT0_EE = comdat any

$_ZNSt11_Tuple_implILm0EJiiEE7_M_headERS0_ = comdat any

$_ZNSt10_Head_baseILm0EmLb0EEC2IiEEOT_ = comdat any

$_ZSt4moveIRSt11_Tuple_implILm2EJEEEONSt16remove_referenceIT_E4typeEOS4_ = comdat any

$_ZNSt11_Tuple_implILm1EJiEE7_M_tailERS0_ = comdat any

$_ZNSt11_Tuple_implILm1EJiEE7_M_headERS0_ = comdat any

$_ZNSt10_Head_baseILm1EmLb0EEC2IiEEOT_ = comdat any

$_ZNSt10_Head_baseILm1EiLb0EE7_M_headERS0_ = comdat any

$_ZNSt10_Head_baseILm0EiLb0EE7_M_headERS0_ = comdat any

$_ZNSt5tupleIJiiiEEC2IJiiiEvEEDpOT_ = comdat any

$_ZNSt11_Tuple_implILm0EJiiiEEC2IiJiiEvEEOT_DpOT0_ = comdat any

$_ZNSt11_Tuple_implILm1EJiiEEC2IiJiEvEEOT_DpOT0_ = comdat any

$_ZNSt11_Tuple_implILm2EJiEEC2IiJEvEEOT_DpOT0_ = comdat any

$_ZNSt10_Head_baseILm2EiLb0EEC2IiEEOT_ = comdat any

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
  %_M_elems = getelementptr inbounds %"struct.std::array", %"struct.std::array"* %agg.result, i32 0, i32 0
  %arrayinit.begin = getelementptr inbounds [3 x i64], [3 x i64]* %_M_elems, i64 0, i64 0
  store i64 0, i64* %arrayinit.begin
  %arrayinit.element = getelementptr inbounds i64, i64* %arrayinit.begin, i64 1
  store i64 0, i64* %arrayinit.element
  %arrayinit.element1 = getelementptr inbounds i64, i64* %arrayinit.element, i64 1
  store i64 0, i64* %arrayinit.element1
  ret void
}

; Function Attrs: nounwind uwtable
define void @_Z9fourarrayv(%"struct.std::array"* noalias sret %agg.result) #0 {
entry:
  call void @llvm.trap()
  unreachable

return:                                           ; No predecessors!
  ret void
}

; Function Attrs: noreturn nounwind
declare void @llvm.trap() #2

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

; Function Attrs: uwtable
define { i64, i64 } @_Z8fo_tuplev() #3 {
entry:
  %retval = alloca %"struct.std::pair", align 8
  %ref.tmp = alloca %"struct.std::pair.2", align 4
  %ref.tmp1 = alloca i32, align 4
  %ref.tmp2 = alloca i32, align 4
  store i32 1, i32* %ref.tmp1
  store i32 -1, i32* %ref.tmp2
  %call = call i64 @_ZSt9make_pairIiiESt4pairINSt17__decay_and_stripIT_E6__typeENS1_IT0_E6__typeEEOS2_OS5_(i32* dereferenceable(4) %ref.tmp1, i32* dereferenceable(4) %ref.tmp2)
  %0 = bitcast %"struct.std::pair.2"* %ref.tmp to i64*
  store i64 %call, i64* %0, align 1
  call void @_ZNSt4pairImmEC2IiivEEOS_IT_T0_E(%"struct.std::pair"* %retval, %"struct.std::pair.2"* dereferenceable(8) %ref.tmp)
  %1 = bitcast %"struct.std::pair"* %retval to { i64, i64 }*
  %2 = load { i64, i64 }, { i64, i64 }* %1, align 1
  ret { i64, i64 } %2
}

; Function Attrs: uwtable
define linkonce_odr i64 @_ZSt9make_pairIiiESt4pairINSt17__decay_and_stripIT_E6__typeENS1_IT0_E6__typeEEOS2_OS5_(i32* dereferenceable(4) %__x, i32* dereferenceable(4) %__y) #3 comdat {
entry:
  %retval = alloca %"struct.std::pair.2", align 4
  %__x.addr = alloca i32*, align 8
  %__y.addr = alloca i32*, align 8
  store i32* %__x, i32** %__x.addr, align 8
  store i32* %__y, i32** %__y.addr, align 8
  %0 = load i32*, i32** %__x.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32*, i32** %__y.addr, align 8
  %call1 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  call void @_ZNSt4pairIiiEC2IiivEEOT_OT0_(%"struct.std::pair.2"* %retval, i32* dereferenceable(4) %call, i32* dereferenceable(4) %call1)
  %2 = bitcast %"struct.std::pair.2"* %retval to i64*
  %3 = load i64, i64* %2, align 1
  ret i64 %3
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt4pairImmEC2IiivEEOS_IT_T0_E(%"struct.std::pair"* %this, %"struct.std::pair.2"* dereferenceable(8) %__p) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::pair"*, align 8
  %__p.addr = alloca %"struct.std::pair.2"*, align 8
  store %"struct.std::pair"* %this, %"struct.std::pair"** %this.addr, align 8
  store %"struct.std::pair.2"* %__p, %"struct.std::pair.2"** %__p.addr, align 8
  %this1 = load %"struct.std::pair"*, %"struct.std::pair"** %this.addr
  %first = getelementptr inbounds %"struct.std::pair", %"struct.std::pair"* %this1, i32 0, i32 0
  %0 = load %"struct.std::pair.2"*, %"struct.std::pair.2"** %__p.addr, align 8
  %first2 = getelementptr inbounds %"struct.std::pair.2", %"struct.std::pair.2"* %0, i32 0, i32 0
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %first2) #1
  %1 = load i32, i32* %call
  %conv = sext i32 %1 to i64
  store i64 %conv, i64* %first, align 8
  %second = getelementptr inbounds %"struct.std::pair", %"struct.std::pair"* %this1, i32 0, i32 1
  %2 = load %"struct.std::pair.2"*, %"struct.std::pair.2"** %__p.addr, align 8
  %second3 = getelementptr inbounds %"struct.std::pair.2", %"struct.std::pair.2"* %2, i32 0, i32 1
  %call4 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %second3) #1
  %3 = load i32, i32* %call4
  %conv5 = sext i32 %3 to i64
  store i64 %conv5, i64* %second, align 8
  ret void
}

; Function Attrs: uwtable
define { i64, i64 } @_Z9foo_tuplev() #3 {
entry:
  %retval = alloca %"class.std::tuple", align 8
  %ref.tmp = alloca %"class.std::tuple.6", align 4
  %ref.tmp1 = alloca i32, align 4
  %ref.tmp2 = alloca i32, align 4
  store i32 1, i32* %ref.tmp1
  store i32 -1, i32* %ref.tmp2
  %call = call i64 @_ZSt10make_tupleIJiiEESt5tupleIJDpNSt17__decay_and_stripIT_E6__typeEEEDpOS2_(i32* dereferenceable(4) %ref.tmp1, i32* dereferenceable(4) %ref.tmp2)
  %coerce.dive = getelementptr %"class.std::tuple.6", %"class.std::tuple.6"* %ref.tmp, i32 0, i32 0
  %0 = bitcast %"struct.std::_Tuple_impl.7"* %coerce.dive to i64*
  store i64 %call, i64* %0, align 1
  call void @_ZNSt5tupleIJmmEEC2IiivEEOS_IJT_T0_EE(%"class.std::tuple"* %retval, %"class.std::tuple.6"* dereferenceable(8) %ref.tmp)
  %coerce.dive3 = getelementptr %"class.std::tuple", %"class.std::tuple"* %retval, i32 0, i32 0
  %1 = bitcast %"struct.std::_Tuple_impl"* %coerce.dive3 to { i64, i64 }*
  %2 = load { i64, i64 }, { i64, i64 }* %1, align 1
  ret { i64, i64 } %2
}

; Function Attrs: uwtable
define linkonce_odr i64 @_ZSt10make_tupleIJiiEESt5tupleIJDpNSt17__decay_and_stripIT_E6__typeEEEDpOS2_(i32* dereferenceable(4) %__args, i32* dereferenceable(4) %__args1) #3 comdat {
entry:
  %retval = alloca %"class.std::tuple.6", align 4
  %__args.addr = alloca i32*, align 8
  %__args.addr2 = alloca i32*, align 8
  store i32* %__args, i32** %__args.addr, align 8
  store i32* %__args1, i32** %__args.addr2, align 8
  %0 = load i32*, i32** %__args.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32*, i32** %__args.addr2, align 8
  %call3 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  call void @_ZNSt5tupleIJiiEEC2IiivEEOT_OT0_(%"class.std::tuple.6"* %retval, i32* dereferenceable(4) %call, i32* dereferenceable(4) %call3)
  %coerce.dive = getelementptr %"class.std::tuple.6", %"class.std::tuple.6"* %retval, i32 0, i32 0
  %2 = bitcast %"struct.std::_Tuple_impl.7"* %coerce.dive to i64*
  %3 = load i64, i64* %2, align 1
  ret i64 %3
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt5tupleIJmmEEC2IiivEEOS_IJT_T0_EE(%"class.std::tuple"* %this, %"class.std::tuple.6"* dereferenceable(8) %__in) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"class.std::tuple"*, align 8
  %__in.addr = alloca %"class.std::tuple.6"*, align 8
  store %"class.std::tuple"* %this, %"class.std::tuple"** %this.addr, align 8
  store %"class.std::tuple.6"* %__in, %"class.std::tuple.6"** %__in.addr, align 8
  %this1 = load %"class.std::tuple"*, %"class.std::tuple"** %this.addr
  %0 = bitcast %"class.std::tuple"* %this1 to %"struct.std::_Tuple_impl"*
  %1 = load %"class.std::tuple.6"*, %"class.std::tuple.6"** %__in.addr, align 8
  %2 = bitcast %"class.std::tuple.6"* %1 to %"struct.std::_Tuple_impl.7"*
  call void @_ZNSt11_Tuple_implILm0EJmmEEC2IiJiEEEOS_ILm0EJT_DpT0_EE(%"struct.std::_Tuple_impl"* %0, %"struct.std::_Tuple_impl.7"* dereferenceable(8) %2)
  ret void
}

; Function Attrs: uwtable
define void @_Z12foooer_tuplev(%"class.std::tuple.11"* noalias sret %agg.result) #3 {
entry:
  %ref.tmp = alloca i32, align 4
  %ref.tmp1 = alloca i32, align 4
  %ref.tmp2 = alloca i32, align 4
  %coerce = alloca %"class.std::tuple.18", align 4
  %tmp = alloca { i64, i32 }
  store i32 1, i32* %ref.tmp
  store i32 -1, i32* %ref.tmp1
  store i32 424, i32* %ref.tmp2
  %call = call { i64, i32 } @_ZSt10make_tupleIJiiiEESt5tupleIJDpNSt17__decay_and_stripIT_E6__typeEEEDpOS2_(i32* dereferenceable(4) %ref.tmp, i32* dereferenceable(4) %ref.tmp1, i32* dereferenceable(4) %ref.tmp2)
  %coerce.dive = getelementptr %"class.std::tuple.18", %"class.std::tuple.18"* %coerce, i32 0, i32 0
  store { i64, i32 } %call, { i64, i32 }* %tmp
  %0 = bitcast { i64, i32 }* %tmp to i8*
  %1 = bitcast %"struct.std::_Tuple_impl.19"* %coerce.dive to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %1, i8* %0, i64 12, i32 1, i1 false)
  call void @llvm.trap()
  unreachable

return:                                           ; No predecessors!
  ret void
}

; Function Attrs: uwtable
define linkonce_odr { i64, i32 } @_ZSt10make_tupleIJiiiEESt5tupleIJDpNSt17__decay_and_stripIT_E6__typeEEEDpOS2_(i32* dereferenceable(4) %__args, i32* dereferenceable(4) %__args1, i32* dereferenceable(4) %__args3) #3 comdat {
entry:
  %retval = alloca %"class.std::tuple.18", align 4
  %__args.addr = alloca i32*, align 8
  %__args.addr2 = alloca i32*, align 8
  %__args.addr4 = alloca i32*, align 8
  %tmp = alloca { i64, i32 }
  store i32* %__args, i32** %__args.addr, align 8
  store i32* %__args1, i32** %__args.addr2, align 8
  store i32* %__args3, i32** %__args.addr4, align 8
  %0 = load i32*, i32** %__args.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32*, i32** %__args.addr2, align 8
  %call5 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  %2 = load i32*, i32** %__args.addr4, align 8
  %call6 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  call void @_ZNSt5tupleIJiiiEEC2IJiiiEvEEDpOT_(%"class.std::tuple.18"* %retval, i32* dereferenceable(4) %call, i32* dereferenceable(4) %call5, i32* dereferenceable(4) %call6)
  %coerce.dive = getelementptr %"class.std::tuple.18", %"class.std::tuple.18"* %retval, i32 0, i32 0
  %3 = bitcast { i64, i32 }* %tmp to i8*
  %4 = bitcast %"struct.std::_Tuple_impl.19"* %coerce.dive to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* %3, i8* %4, i64 12, i32 1, i1 false)
  %5 = load { i64, i32 }, { i64, i32 }* %tmp
  ret { i64, i32 } %5
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %__t) #0 comdat {
entry:
  %__t.addr = alloca i32*, align 8
  store i32* %__t, i32** %__t.addr, align 8
  %0 = load i32*, i32** %__t.addr, align 8
  ret i32* %0
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt4pairIiiEC2IiivEEOT_OT0_(%"struct.std::pair.2"* %this, i32* dereferenceable(4) %__x, i32* dereferenceable(4) %__y) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::pair.2"*, align 8
  %__x.addr = alloca i32*, align 8
  %__y.addr = alloca i32*, align 8
  store %"struct.std::pair.2"* %this, %"struct.std::pair.2"** %this.addr, align 8
  store i32* %__x, i32** %__x.addr, align 8
  store i32* %__y, i32** %__y.addr, align 8
  %this1 = load %"struct.std::pair.2"*, %"struct.std::pair.2"** %this.addr
  %first = getelementptr inbounds %"struct.std::pair.2", %"struct.std::pair.2"* %this1, i32 0, i32 0
  %0 = load i32*, i32** %__x.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32, i32* %call
  store i32 %1, i32* %first, align 4
  %second = getelementptr inbounds %"struct.std::pair.2", %"struct.std::pair.2"* %this1, i32 0, i32 1
  %2 = load i32*, i32** %__y.addr, align 8
  %call2 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  %3 = load i32, i32* %call2
  store i32 %3, i32* %second, align 4
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt5tupleIJiiEEC2IiivEEOT_OT0_(%"class.std::tuple.6"* %this, i32* dereferenceable(4) %__a1, i32* dereferenceable(4) %__a2) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"class.std::tuple.6"*, align 8
  %__a1.addr = alloca i32*, align 8
  %__a2.addr = alloca i32*, align 8
  store %"class.std::tuple.6"* %this, %"class.std::tuple.6"** %this.addr, align 8
  store i32* %__a1, i32** %__a1.addr, align 8
  store i32* %__a2, i32** %__a2.addr, align 8
  %this1 = load %"class.std::tuple.6"*, %"class.std::tuple.6"** %this.addr
  %0 = bitcast %"class.std::tuple.6"* %this1 to %"struct.std::_Tuple_impl.7"*
  %1 = load i32*, i32** %__a1.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  %2 = load i32*, i32** %__a2.addr, align 8
  %call2 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  call void @_ZNSt11_Tuple_implILm0EJiiEEC2IiJiEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.7"* %0, i32* dereferenceable(4) %call, i32* dereferenceable(4) %call2)
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm0EJiiEEC2IiJiEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.7"* %this, i32* dereferenceable(4) %__head, i32* dereferenceable(4) %__tail) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl.7"*, align 8
  %__head.addr = alloca i32*, align 8
  %__tail.addr = alloca i32*, align 8
  store %"struct.std::_Tuple_impl.7"* %this, %"struct.std::_Tuple_impl.7"** %this.addr, align 8
  store i32* %__head, i32** %__head.addr, align 8
  store i32* %__tail, i32** %__tail.addr, align 8
  %this1 = load %"struct.std::_Tuple_impl.7"*, %"struct.std::_Tuple_impl.7"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl.7"* %this1 to %"struct.std::_Tuple_impl.8"*
  %1 = load i32*, i32** %__tail.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  call void @_ZNSt11_Tuple_implILm1EJiEEC2IiJEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.8"* %0, i32* dereferenceable(4) %call)
  %2 = bitcast %"struct.std::_Tuple_impl.7"* %this1 to i8*
  %3 = getelementptr inbounds i8, i8* %2, i64 4
  %4 = bitcast i8* %3 to %"struct.std::_Head_base.10"*
  %5 = load i32*, i32** %__head.addr, align 8
  %call2 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %5) #1
  call void @_ZNSt10_Head_baseILm0EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.10"* %4, i32* dereferenceable(4) %call2)
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm1EJiEEC2IiJEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.8"* %this, i32* dereferenceable(4) %__head) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl.8"*, align 8
  %__head.addr = alloca i32*, align 8
  store %"struct.std::_Tuple_impl.8"* %this, %"struct.std::_Tuple_impl.8"** %this.addr, align 8
  store i32* %__head, i32** %__head.addr, align 8
  %this1 = load %"struct.std::_Tuple_impl.8"*, %"struct.std::_Tuple_impl.8"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl.8"* %this1 to %"struct.std::_Tuple_impl.4"*
  %1 = bitcast %"struct.std::_Tuple_impl.8"* %this1 to %"struct.std::_Head_base.9"*
  %2 = load i32*, i32** %__head.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  call void @_ZNSt10_Head_baseILm1EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.9"* %1, i32* dereferenceable(4) %call)
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt10_Head_baseILm0EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.10"* %this, i32* dereferenceable(4) %__h) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Head_base.10"*, align 8
  %__h.addr = alloca i32*, align 8
  store %"struct.std::_Head_base.10"* %this, %"struct.std::_Head_base.10"** %this.addr, align 8
  store i32* %__h, i32** %__h.addr, align 8
  %this1 = load %"struct.std::_Head_base.10"*, %"struct.std::_Head_base.10"** %this.addr
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base.10", %"struct.std::_Head_base.10"* %this1, i32 0, i32 0
  %0 = load i32*, i32** %__h.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32, i32* %call
  store i32 %1, i32* %_M_head_impl, align 4
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt10_Head_baseILm1EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.9"* %this, i32* dereferenceable(4) %__h) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Head_base.9"*, align 8
  %__h.addr = alloca i32*, align 8
  store %"struct.std::_Head_base.9"* %this, %"struct.std::_Head_base.9"** %this.addr, align 8
  store i32* %__h, i32** %__h.addr, align 8
  %this1 = load %"struct.std::_Head_base.9"*, %"struct.std::_Head_base.9"** %this.addr
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base.9", %"struct.std::_Head_base.9"* %this1, i32 0, i32 0
  %0 = load i32*, i32** %__h.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32, i32* %call
  store i32 %1, i32* %_M_head_impl, align 4
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm0EJmmEEC2IiJiEEEOS_ILm0EJT_DpT0_EE(%"struct.std::_Tuple_impl"* %this, %"struct.std::_Tuple_impl.7"* dereferenceable(8) %__in) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl"*, align 8
  %__in.addr = alloca %"struct.std::_Tuple_impl.7"*, align 8
  store %"struct.std::_Tuple_impl"* %this, %"struct.std::_Tuple_impl"** %this.addr, align 8
  store %"struct.std::_Tuple_impl.7"* %__in, %"struct.std::_Tuple_impl.7"** %__in.addr, align 8
  %this1 = load %"struct.std::_Tuple_impl"*, %"struct.std::_Tuple_impl"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl"* %this1 to %"struct.std::_Tuple_impl.3"*
  %1 = load %"struct.std::_Tuple_impl.7"*, %"struct.std::_Tuple_impl.7"** %__in.addr, align 8
  %call = call dereferenceable(4) %"struct.std::_Tuple_impl.8"* @_ZNSt11_Tuple_implILm0EJiiEE7_M_tailERS0_(%"struct.std::_Tuple_impl.7"* dereferenceable(8) %1) #1
  %call2 = call dereferenceable(4) %"struct.std::_Tuple_impl.8"* @_ZSt4moveIRSt11_Tuple_implILm1EJiEEEONSt16remove_referenceIT_E4typeEOS4_(%"struct.std::_Tuple_impl.8"* dereferenceable(4) %call) #1
  call void @_ZNSt11_Tuple_implILm1EJmEEC2IiJEEEOS_ILm1EJT_DpT0_EE(%"struct.std::_Tuple_impl.3"* %0, %"struct.std::_Tuple_impl.8"* dereferenceable(4) %call2)
  %2 = bitcast %"struct.std::_Tuple_impl"* %this1 to i8*
  %3 = getelementptr inbounds i8, i8* %2, i64 8
  %4 = bitcast i8* %3 to %"struct.std::_Head_base.5"*
  %5 = load %"struct.std::_Tuple_impl.7"*, %"struct.std::_Tuple_impl.7"** %__in.addr, align 8
  %call3 = call dereferenceable(4) i32* @_ZNSt11_Tuple_implILm0EJiiEE7_M_headERS0_(%"struct.std::_Tuple_impl.7"* dereferenceable(8) %5) #1
  %call4 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %call3) #1
  call void @_ZNSt10_Head_baseILm0EmLb0EEC2IiEEOT_(%"struct.std::_Head_base.5"* %4, i32* dereferenceable(4) %call4)
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) %"struct.std::_Tuple_impl.8"* @_ZSt4moveIRSt11_Tuple_implILm1EJiEEEONSt16remove_referenceIT_E4typeEOS4_(%"struct.std::_Tuple_impl.8"* dereferenceable(4) %__t) #0 comdat {
entry:
  %__t.addr = alloca %"struct.std::_Tuple_impl.8"*, align 8
  store %"struct.std::_Tuple_impl.8"* %__t, %"struct.std::_Tuple_impl.8"** %__t.addr, align 8
  %0 = load %"struct.std::_Tuple_impl.8"*, %"struct.std::_Tuple_impl.8"** %__t.addr, align 8
  ret %"struct.std::_Tuple_impl.8"* %0
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) %"struct.std::_Tuple_impl.8"* @_ZNSt11_Tuple_implILm0EJiiEE7_M_tailERS0_(%"struct.std::_Tuple_impl.7"* dereferenceable(8) %__t) #0 comdat align 2 {
entry:
  %__t.addr = alloca %"struct.std::_Tuple_impl.7"*, align 8
  store %"struct.std::_Tuple_impl.7"* %__t, %"struct.std::_Tuple_impl.7"** %__t.addr, align 8
  %0 = load %"struct.std::_Tuple_impl.7"*, %"struct.std::_Tuple_impl.7"** %__t.addr, align 8
  %1 = bitcast %"struct.std::_Tuple_impl.7"* %0 to %"struct.std::_Tuple_impl.8"*
  ret %"struct.std::_Tuple_impl.8"* %1
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm1EJmEEC2IiJEEEOS_ILm1EJT_DpT0_EE(%"struct.std::_Tuple_impl.3"* %this, %"struct.std::_Tuple_impl.8"* dereferenceable(4) %__in) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl.3"*, align 8
  %__in.addr = alloca %"struct.std::_Tuple_impl.8"*, align 8
  store %"struct.std::_Tuple_impl.3"* %this, %"struct.std::_Tuple_impl.3"** %this.addr, align 8
  store %"struct.std::_Tuple_impl.8"* %__in, %"struct.std::_Tuple_impl.8"** %__in.addr, align 8
  %this1 = load %"struct.std::_Tuple_impl.3"*, %"struct.std::_Tuple_impl.3"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl.3"* %this1 to %"struct.std::_Tuple_impl.4"*
  %1 = load %"struct.std::_Tuple_impl.8"*, %"struct.std::_Tuple_impl.8"** %__in.addr, align 8
  %call = call dereferenceable(1) %"struct.std::_Tuple_impl.4"* @_ZNSt11_Tuple_implILm1EJiEE7_M_tailERS0_(%"struct.std::_Tuple_impl.8"* dereferenceable(4) %1) #1
  %call2 = call dereferenceable(1) %"struct.std::_Tuple_impl.4"* @_ZSt4moveIRSt11_Tuple_implILm2EJEEEONSt16remove_referenceIT_E4typeEOS4_(%"struct.std::_Tuple_impl.4"* dereferenceable(1) %call) #1
  %2 = bitcast %"struct.std::_Tuple_impl.3"* %this1 to %"struct.std::_Head_base"*
  %3 = load %"struct.std::_Tuple_impl.8"*, %"struct.std::_Tuple_impl.8"** %__in.addr, align 8
  %call3 = call dereferenceable(4) i32* @_ZNSt11_Tuple_implILm1EJiEE7_M_headERS0_(%"struct.std::_Tuple_impl.8"* dereferenceable(4) %3) #1
  %call4 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %call3) #1
  call void @_ZNSt10_Head_baseILm1EmLb0EEC2IiEEOT_(%"struct.std::_Head_base"* %2, i32* dereferenceable(4) %call4)
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) i32* @_ZNSt11_Tuple_implILm0EJiiEE7_M_headERS0_(%"struct.std::_Tuple_impl.7"* dereferenceable(8) %__t) #0 comdat align 2 {
entry:
  %__t.addr = alloca %"struct.std::_Tuple_impl.7"*, align 8
  store %"struct.std::_Tuple_impl.7"* %__t, %"struct.std::_Tuple_impl.7"** %__t.addr, align 8
  %0 = load %"struct.std::_Tuple_impl.7"*, %"struct.std::_Tuple_impl.7"** %__t.addr, align 8
  %1 = bitcast %"struct.std::_Tuple_impl.7"* %0 to i8*
  %add.ptr = getelementptr inbounds i8, i8* %1, i64 4
  %2 = bitcast i8* %add.ptr to %"struct.std::_Head_base.10"*
  %call = call dereferenceable(4) i32* @_ZNSt10_Head_baseILm0EiLb0EE7_M_headERS0_(%"struct.std::_Head_base.10"* dereferenceable(4) %2) #1
  ret i32* %call
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt10_Head_baseILm0EmLb0EEC2IiEEOT_(%"struct.std::_Head_base.5"* %this, i32* dereferenceable(4) %__h) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Head_base.5"*, align 8
  %__h.addr = alloca i32*, align 8
  store %"struct.std::_Head_base.5"* %this, %"struct.std::_Head_base.5"** %this.addr, align 8
  store i32* %__h, i32** %__h.addr, align 8
  %this1 = load %"struct.std::_Head_base.5"*, %"struct.std::_Head_base.5"** %this.addr
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base.5", %"struct.std::_Head_base.5"* %this1, i32 0, i32 0
  %0 = load i32*, i32** %__h.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32, i32* %call
  %conv = sext i32 %1 to i64
  store i64 %conv, i64* %_M_head_impl, align 8
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(1) %"struct.std::_Tuple_impl.4"* @_ZSt4moveIRSt11_Tuple_implILm2EJEEEONSt16remove_referenceIT_E4typeEOS4_(%"struct.std::_Tuple_impl.4"* dereferenceable(1) %__t) #0 comdat {
entry:
  %__t.addr = alloca %"struct.std::_Tuple_impl.4"*, align 8
  store %"struct.std::_Tuple_impl.4"* %__t, %"struct.std::_Tuple_impl.4"** %__t.addr, align 8
  %0 = load %"struct.std::_Tuple_impl.4"*, %"struct.std::_Tuple_impl.4"** %__t.addr, align 8
  ret %"struct.std::_Tuple_impl.4"* %0
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(1) %"struct.std::_Tuple_impl.4"* @_ZNSt11_Tuple_implILm1EJiEE7_M_tailERS0_(%"struct.std::_Tuple_impl.8"* dereferenceable(4) %__t) #0 comdat align 2 {
entry:
  %__t.addr = alloca %"struct.std::_Tuple_impl.8"*, align 8
  store %"struct.std::_Tuple_impl.8"* %__t, %"struct.std::_Tuple_impl.8"** %__t.addr, align 8
  %0 = load %"struct.std::_Tuple_impl.8"*, %"struct.std::_Tuple_impl.8"** %__t.addr, align 8
  %1 = bitcast %"struct.std::_Tuple_impl.8"* %0 to %"struct.std::_Tuple_impl.4"*
  ret %"struct.std::_Tuple_impl.4"* %1
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) i32* @_ZNSt11_Tuple_implILm1EJiEE7_M_headERS0_(%"struct.std::_Tuple_impl.8"* dereferenceable(4) %__t) #0 comdat align 2 {
entry:
  %__t.addr = alloca %"struct.std::_Tuple_impl.8"*, align 8
  store %"struct.std::_Tuple_impl.8"* %__t, %"struct.std::_Tuple_impl.8"** %__t.addr, align 8
  %0 = load %"struct.std::_Tuple_impl.8"*, %"struct.std::_Tuple_impl.8"** %__t.addr, align 8
  %1 = bitcast %"struct.std::_Tuple_impl.8"* %0 to %"struct.std::_Head_base.9"*
  %call = call dereferenceable(4) i32* @_ZNSt10_Head_baseILm1EiLb0EE7_M_headERS0_(%"struct.std::_Head_base.9"* dereferenceable(4) %1) #1
  ret i32* %call
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt10_Head_baseILm1EmLb0EEC2IiEEOT_(%"struct.std::_Head_base"* %this, i32* dereferenceable(4) %__h) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Head_base"*, align 8
  %__h.addr = alloca i32*, align 8
  store %"struct.std::_Head_base"* %this, %"struct.std::_Head_base"** %this.addr, align 8
  store i32* %__h, i32** %__h.addr, align 8
  %this1 = load %"struct.std::_Head_base"*, %"struct.std::_Head_base"** %this.addr
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base", %"struct.std::_Head_base"* %this1, i32 0, i32 0
  %0 = load i32*, i32** %__h.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32, i32* %call
  %conv = sext i32 %1 to i64
  store i64 %conv, i64* %_M_head_impl, align 8
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) i32* @_ZNSt10_Head_baseILm1EiLb0EE7_M_headERS0_(%"struct.std::_Head_base.9"* dereferenceable(4) %__b) #0 comdat align 2 {
entry:
  %__b.addr = alloca %"struct.std::_Head_base.9"*, align 8
  store %"struct.std::_Head_base.9"* %__b, %"struct.std::_Head_base.9"** %__b.addr, align 8
  %0 = load %"struct.std::_Head_base.9"*, %"struct.std::_Head_base.9"** %__b.addr, align 8
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base.9", %"struct.std::_Head_base.9"* %0, i32 0, i32 0
  ret i32* %_M_head_impl
}

; Function Attrs: nounwind uwtable
define linkonce_odr dereferenceable(4) i32* @_ZNSt10_Head_baseILm0EiLb0EE7_M_headERS0_(%"struct.std::_Head_base.10"* dereferenceable(4) %__b) #0 comdat align 2 {
entry:
  %__b.addr = alloca %"struct.std::_Head_base.10"*, align 8
  store %"struct.std::_Head_base.10"* %__b, %"struct.std::_Head_base.10"** %__b.addr, align 8
  %0 = load %"struct.std::_Head_base.10"*, %"struct.std::_Head_base.10"** %__b.addr, align 8
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base.10", %"struct.std::_Head_base.10"* %0, i32 0, i32 0
  ret i32* %_M_head_impl
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt5tupleIJiiiEEC2IJiiiEvEEDpOT_(%"class.std::tuple.18"* %this, i32* dereferenceable(4) %__elements, i32* dereferenceable(4) %__elements1, i32* dereferenceable(4) %__elements3) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"class.std::tuple.18"*, align 8
  %__elements.addr = alloca i32*, align 8
  %__elements.addr2 = alloca i32*, align 8
  %__elements.addr4 = alloca i32*, align 8
  store %"class.std::tuple.18"* %this, %"class.std::tuple.18"** %this.addr, align 8
  store i32* %__elements, i32** %__elements.addr, align 8
  store i32* %__elements1, i32** %__elements.addr2, align 8
  store i32* %__elements3, i32** %__elements.addr4, align 8
  %this5 = load %"class.std::tuple.18"*, %"class.std::tuple.18"** %this.addr
  %0 = bitcast %"class.std::tuple.18"* %this5 to %"struct.std::_Tuple_impl.19"*
  %1 = load i32*, i32** %__elements.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  %2 = load i32*, i32** %__elements.addr2, align 8
  %call6 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  %3 = load i32*, i32** %__elements.addr4, align 8
  %call7 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %3) #1
  call void @_ZNSt11_Tuple_implILm0EJiiiEEC2IiJiiEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.19"* %0, i32* dereferenceable(4) %call, i32* dereferenceable(4) %call6, i32* dereferenceable(4) %call7)
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm0EJiiiEEC2IiJiiEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.19"* %this, i32* dereferenceable(4) %__head, i32* dereferenceable(4) %__tail, i32* dereferenceable(4) %__tail1) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl.19"*, align 8
  %__head.addr = alloca i32*, align 8
  %__tail.addr = alloca i32*, align 8
  %__tail.addr2 = alloca i32*, align 8
  store %"struct.std::_Tuple_impl.19"* %this, %"struct.std::_Tuple_impl.19"** %this.addr, align 8
  store i32* %__head, i32** %__head.addr, align 8
  store i32* %__tail, i32** %__tail.addr, align 8
  store i32* %__tail1, i32** %__tail.addr2, align 8
  %this3 = load %"struct.std::_Tuple_impl.19"*, %"struct.std::_Tuple_impl.19"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl.19"* %this3 to %"struct.std::_Tuple_impl.20"*
  %1 = load i32*, i32** %__tail.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  %2 = load i32*, i32** %__tail.addr2, align 8
  %call4 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  call void @_ZNSt11_Tuple_implILm1EJiiEEC2IiJiEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.20"* %0, i32* dereferenceable(4) %call, i32* dereferenceable(4) %call4)
  %3 = bitcast %"struct.std::_Tuple_impl.19"* %this3 to i8*
  %4 = getelementptr inbounds i8, i8* %3, i64 8
  %5 = bitcast i8* %4 to %"struct.std::_Head_base.10"*
  %6 = load i32*, i32** %__head.addr, align 8
  %call5 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %6) #1
  call void @_ZNSt10_Head_baseILm0EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.10"* %5, i32* dereferenceable(4) %call5)
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm1EJiiEEC2IiJiEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.20"* %this, i32* dereferenceable(4) %__head, i32* dereferenceable(4) %__tail) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl.20"*, align 8
  %__head.addr = alloca i32*, align 8
  %__tail.addr = alloca i32*, align 8
  store %"struct.std::_Tuple_impl.20"* %this, %"struct.std::_Tuple_impl.20"** %this.addr, align 8
  store i32* %__head, i32** %__head.addr, align 8
  store i32* %__tail, i32** %__tail.addr, align 8
  %this1 = load %"struct.std::_Tuple_impl.20"*, %"struct.std::_Tuple_impl.20"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl.20"* %this1 to %"struct.std::_Tuple_impl.21"*
  %1 = load i32*, i32** %__tail.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %1) #1
  call void @_ZNSt11_Tuple_implILm2EJiEEC2IiJEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.21"* %0, i32* dereferenceable(4) %call)
  %2 = bitcast %"struct.std::_Tuple_impl.20"* %this1 to i8*
  %3 = getelementptr inbounds i8, i8* %2, i64 4
  %4 = bitcast i8* %3 to %"struct.std::_Head_base.9"*
  %5 = load i32*, i32** %__head.addr, align 8
  %call2 = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %5) #1
  call void @_ZNSt10_Head_baseILm1EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.9"* %4, i32* dereferenceable(4) %call2)
  ret void
}

; Function Attrs: uwtable
define linkonce_odr void @_ZNSt11_Tuple_implILm2EJiEEC2IiJEvEEOT_DpOT0_(%"struct.std::_Tuple_impl.21"* %this, i32* dereferenceable(4) %__head) unnamed_addr #3 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Tuple_impl.21"*, align 8
  %__head.addr = alloca i32*, align 8
  store %"struct.std::_Tuple_impl.21"* %this, %"struct.std::_Tuple_impl.21"** %this.addr, align 8
  store i32* %__head, i32** %__head.addr, align 8
  %this1 = load %"struct.std::_Tuple_impl.21"*, %"struct.std::_Tuple_impl.21"** %this.addr
  %0 = bitcast %"struct.std::_Tuple_impl.21"* %this1 to %"struct.std::_Tuple_impl.15"*
  %1 = bitcast %"struct.std::_Tuple_impl.21"* %this1 to %"struct.std::_Head_base.22"*
  %2 = load i32*, i32** %__head.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %2) #1
  call void @_ZNSt10_Head_baseILm2EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.22"* %1, i32* dereferenceable(4) %call)
  ret void
}

; Function Attrs: nounwind uwtable
define linkonce_odr void @_ZNSt10_Head_baseILm2EiLb0EEC2IiEEOT_(%"struct.std::_Head_base.22"* %this, i32* dereferenceable(4) %__h) unnamed_addr #0 comdat align 2 {
entry:
  %this.addr = alloca %"struct.std::_Head_base.22"*, align 8
  %__h.addr = alloca i32*, align 8
  store %"struct.std::_Head_base.22"* %this, %"struct.std::_Head_base.22"** %this.addr, align 8
  store i32* %__h, i32** %__h.addr, align 8
  %this1 = load %"struct.std::_Head_base.22"*, %"struct.std::_Head_base.22"** %this.addr
  %_M_head_impl = getelementptr inbounds %"struct.std::_Head_base.22", %"struct.std::_Head_base.22"* %this1, i32 0, i32 0
  %0 = load i32*, i32** %__h.addr, align 8
  %call = call dereferenceable(4) i32* @_ZSt7forwardIiEOT_RNSt16remove_referenceIS0_E4typeE(i32* dereferenceable(4) %0) #1
  %1 = load i32, i32* %call
  store i32 %1, i32* %_M_head_impl, align 4
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
attributes #3 = { uwtable "disable-tail-calls"="false" "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+sse,+sse2" "unsafe-fp-math"="false" "use-soft-float"="false" }

!llvm.ident = !{!0}

!0 = !{!"clang version 3.7.0 (trunk 241399)"}
