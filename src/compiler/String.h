#ifndef STRING_H
#define STRING_H

#define LOAD_STRING_LENGTH(PTR) \
    CreateLoad(getInt32Ty(), CreateStructGEP(getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 2), "length")
#define LOAD_STRING_STRING(PTR) \
    CreateLoad(getPtrTy(), CreateStructGEP(getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 1), "string")
#define STORE_STRING_LENGTH(PTR, LENGTH) \
    CreateStore(LENGTH, CreateStructGEP(getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 2))
#define STORE_STRING_STRING(PTR, STR) \
    CreateStore(STR, CreateStructGEP(getStructType(ObjType::STRING), CreateLoad(getPtrTy(), PTR), 1))

#endif//STRING_H
