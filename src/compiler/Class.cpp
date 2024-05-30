
#include "LoxBuilder.h"
#include "Stack.h"

#include <string_view>

using namespace std::string_view_literals;

namespace lox {

    Value *LoxBuilder::AllocateClass(Value *name) {
        auto *const ptr = AllocateObj(ObjType::CLASS, "class");

        auto *const methods = AllocateTable();

        CreateStore(name, CreateObjStructGEP(ObjType::CLASS, ptr, 1));
        CreateStore(methods, CreateObjStructGEP(ObjType::CLASS, ptr, 2));

        return ptr;
    }

    Value *LoxBuilder::AllocateInstance(Value *klass) {
        auto *const ptr = AllocateObj(ObjType::INSTANCE, "instance");

        Value *fields = AllocateTable();

        CreateStore(klass, CreateObjStructGEP(ObjType::INSTANCE, ptr, 1));
        CreateStore(fields, CreateObjStructGEP(ObjType::INSTANCE, ptr, 2));

        return ptr;
    }

    Value *LoxBuilder::BindMethod(Value *klass, Value *receiver, Value *key, const unsigned int line, const llvm::Function *pFunction) {
        assert(klass->getType() == getPtrTy());
        assert(receiver->getType() == getPtrTy());

        static auto *BindMethodFunction([this] {
            auto *const F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy(), getPtrTy(), getPtrTy(), getInt32Ty(), getPtrTy()},
                    false
                ),
                Function::InternalLinkage,
                "$bindMethod",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            auto *const EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            auto *const arguments = F->args().begin();
            auto *const klass = arguments;
            auto *const receiver = arguments + 1;
            auto *const key = arguments + 2;
            auto *const line = arguments + 3;
            auto *const function = arguments + 4;

            auto *const methods = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 2));
            auto *const method = B.TableGet(methods, key);

            auto *const IsUndefinedBlock = B.CreateBasicBlock("property.undefined");
            auto *const IsDefinedBlock = B.CreateBasicBlock("property.defined");

            B.CreateCondBr(B.IsUninitialized(method), IsUndefinedBlock, IsDefinedBlock);

            B.SetInsertPoint(IsUndefinedBlock);
            {
                B.RuntimeError(
                    line,
                    "Undefined property '%s'.\n"sv,
                    {B.AsCString(B.ObjVal(key))},
                    function
                );
                B.CreateUnreachable();
            }
            B.SetInsertPoint(IsDefinedBlock);
            {
                auto *const ptr = B.AllocateObj(ObjType::BOUND_METHOD, "bound_method");
                B.CreateStore(B.ObjVal(receiver), B.CreateObjStructGEP(ObjType::BOUND_METHOD, ptr, 1));
                B.CreateStore(B.AsObj(method), B.CreateObjStructGEP(ObjType::BOUND_METHOD, ptr, 2));
                B.CreateRet(ptr);
            }

            return F;
        }());

        return CreateCall(
            BindMethodFunction,
            {klass, receiver, key, getInt32(line), CreateGlobalCachedString(pFunction == nullptr ? "script" : pFunction->getName())}
        );
    }
}// namespace lox