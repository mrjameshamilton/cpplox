#ifndef STRING_H
#define STRING_H

#define LOAD_STRING_LENGTH(PTR) \
    Builder->CreateLoad(Builder->getInt32Ty(), Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), PTR), 2), "length")
#define LOAD_STRING_STRING(PTR) \
    Builder->CreateLoad(Builder->getPtrTy(), Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), PTR), 1), "string")
#define STORE_STRING_LENGTH(PTR, LENGTH) \
    Builder->CreateStore(LENGTH, Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), PTR), 2))
#define STORE_STRING_STRING(PTR, STRING) \
    Builder->CreateStore(STRING, Builder->CreateStructGEP(StringStructType, Builder->CreateLoad(Builder->getPtrTy(), PTR), 1))

#endif//STRING_H
