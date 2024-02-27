#include "Class.h"

using namespace std::string_view_literals;

namespace lox {

    Value *LoxBuilder::AllocateClass(const std::string_view className) {
        const auto ptr = AllocateObj(ObjType::CLASS, "class");

        Value *name = AllocateString(className, ("class_" + className).str());
        Value *methods = AllocateTable();

        CreateStore(name, CreateObjStructGEP(ObjType::CLASS, ptr, 1));
        CreateStore(methods, CreateObjStructGEP(ObjType::CLASS, ptr, 2));

        return ptr;
    }

    Value *LoxBuilder::AllocateInstance(llvm::Value *klass) {
        const auto ptr = AllocateObj(ObjType::INSTANCE, "instance");

        Value *fields = AllocateTable();

        CreateStore(klass, CreateObjStructGEP(ObjType::INSTANCE, ptr, 1));
        CreateStore(fields, CreateObjStructGEP(ObjType::INSTANCE, ptr, 2));

        return ptr;
    }

    Value *LoxBuilder::BindMethod(Value *klass, Value *receiver, Value *key, unsigned int line) {
        assert(klass->getType() == getPtrTy());
        assert(receiver->getType() == getPtrTy());

        static auto BindMethodFunction([this] {
            const auto F = Function::Create(
                FunctionType::get(
                    getPtrTy(),
                    {getPtrTy(), getPtrTy(), getPtrTy(), getInt32Ty()},
                    false
                ),
                Function::InternalLinkage,
                "$bindMethod",
                getModule()
            );

            LoxBuilder B(getContext(), getModule(), *F);

            const auto EntryBasicBlock = B.CreateBasicBlock("entry");
            B.SetInsertPoint(EntryBasicBlock);

            const auto arguments = F->args().begin();
            const auto klass = arguments;
            const auto receiver = arguments + 1;
            const auto key = arguments + 2;
            const auto line = arguments + 3;

            const auto methods = B.CreateLoad(B.getPtrTy(), B.CreateObjStructGEP(ObjType::CLASS, klass, 2));
            const auto method = B.TableGet(methods, key);

            const auto IsMethodBlock = B.CreateBasicBlock("property.ismethod");
            const auto IsUndefinedBlock = B.CreateBasicBlock("property.undefined");
            const auto IsDefinedBlock = B.CreateBasicBlock("property.defined");

            B.CreateCondBr(B.IsUninitialized(method), IsUndefinedBlock, IsMethodBlock);

            B.SetInsertPoint(IsMethodBlock);

            B.CreateBr(IsDefinedBlock);

            B.SetInsertPoint(IsUndefinedBlock);
            B.RuntimeError(
                line,
                "Undefined property '%s'.\n"sv,
                {B.AsCString(B.ObjVal(key))},
                getFunction()
            );
            B.CreateUnreachable();

            B.SetInsertPoint(IsDefinedBlock);

            const auto ptr = B.AllocateObj(ObjType::BOUND_METHOD, "bound_method");

            B.CreateStore(B.ObjVal(receiver), B.CreateObjStructGEP(ObjType::BOUND_METHOD, ptr, 1));
            B.CreateStore(B.AsObj(method), B.CreateObjStructGEP(ObjType::BOUND_METHOD, ptr, 2));

            B.CreateRet(ptr);

            return F;
        }());

        return CreateCall(BindMethodFunction, {klass, receiver, key, getInt32(line)});
    }
}// namespace lox