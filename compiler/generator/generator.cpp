#include "generator.h"

#include <memory>
#include <iostream>
#include <vector>
#include <llvm/IR/Verifier.h>
#include "llvm/ADT/APInt.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "compiler/models/ast.h"
#include "compiler/models/exceptions.h"
#include "compiler/models/globals.h"

typedef Exceptions::AssertionException AssertionException;
typedef Exceptions::RedeclaredException RedeclaredException;
typedef Exceptions::TypeException TypeException;
typedef Exceptions::UndeclaredException UndeclaredException;

const int CHAR_BIT_SIZE = 32; // putchar() uses int32 rather than int8.
const int INTEGER_BIT_SIZE = 32;

llvm::Function* Generator::gen(const AST::File& file) {
    return Generator().generate(file);
}

llvm::Value* Generator::generate(const AST::AddOpExpression& addition) {
    llvm::Value* left = addition.leftExpr->generate(*this);
    llvm::Value* right = addition.rightExpr->generate(*this);

    return builder.CreateAdd(left, right, "addtmp");
}

llvm::Value* Generator::generate(const AST::SubOpExpression& subtraction) {
    llvm::Value* left = subtraction.leftExpr->generate(*this);
    llvm::Value* right = subtraction.rightExpr->generate(*this);

    return builder.CreateSub(left, right, "subtmp");
}

llvm::Value* Generator::generate(const AST::MulOpExpression& multiplication) {
    llvm::Value* left = multiplication.leftExpr->generate(*this);
    llvm::Value* right = multiplication.rightExpr->generate(*this);

    return builder.CreateMul(left, right, "multmp");
}

llvm::Value* Generator::generate(const AST::DivOpExpression& division) {
    llvm::Value* left = division.leftExpr->generate(*this);
    llvm::Value* right = division.rightExpr->generate(*this);

    return builder.CreateSDiv(left, right, "divtmp");
}

llvm::IntegerType* Generator::generate(const AST::IntegerType& integer) {
    return llvm::IntegerType::getInt32Ty(*context);
}

llvm::PointerType* Generator::generate(const AST::StringType& string) {
    return llvm::PointerType::getInt8PtrTy(*context);
}

llvm::FunctionType* Generator::generate(const AST::FunctionPrototype& prototype) {
    std::vector<llvm::Type*> parameterTypes;
    for (const auto& param : prototype.parameters) {
        parameterTypes.push_back(param->generate(*this));
    }
    llvm::Type* returnType = prototype.returnType->generate(*this);

    return llvm::FunctionType::get(returnType, parameterTypes, false /* isVarArgs */);
}

llvm::Function* Generator::generate(const AST::Function& func) {
    llvm::FunctionType* type = func.type->generate(*this);
    return llvm::Function::Create(type, llvm::Function::ExternalLinkage, func.name, module.get());
}

void Generator::generate(const AST::StatementExpression& stmt) {
    stmt.expr->generate(*this);
}

void Generator::generate(const AST::StatementLet& stmt) {
    if (namedValues[stmt.name]) {
        throw RedeclaredException("Variable \"" + stmt.name + "\" already declared in this scope.");
    }

    llvm::Value* value = stmt.expr->generate(*this);
    llvm::Type* type = stmt.type->generate(*this);
    if (value->getType() != type) throw TypeException("Type mismatch");

    namedValues[stmt.name] = value;
}

llvm::Function* Generator::generate(const AST::File& file) {
    for (const auto& func : file.funcs) {
        func->generate(*this);
    }

    // Stub a main function because one is required.
    const auto params = std::vector<std::shared_ptr<const AST::Type>>();
    const auto returnType = std::make_shared<AST::IntegerType>(AST::IntegerType());
    const auto mainProto = std::make_shared<const AST::FunctionPrototype>(AST::FunctionPrototype(params, returnType));
    const AST::Function mainFunc("main", mainProto);
    llvm::Function* main = mainFunc.generate(*this);

    // Create a new basic block to start insertion into.
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(*context, "entry", main);
    builder.SetInsertPoint(bb);

    // Generate the body of the main function.
    for (const auto& stmt : file.statements) {
        stmt->generate(*this);
    }

    // Return 0 always
    llvm::APInt retVal(INTEGER_BIT_SIZE, (uint32_t) 0, true /* signed */);
    builder.CreateRet(llvm::ConstantInt::get(*context, retVal));

    llvm::verifyFunction(*main);
    return main;
}

llvm::Value* Generator::generate(const AST::CharLiteral& literal) {
    llvm::APInt llvmInt(CHAR_BIT_SIZE, (uint64_t) literal.value, true /* signed */);
    return llvm::ConstantInt::get(*context, llvmInt);
}

llvm::Value* Generator::generate(const AST::IntegerLiteral& literal) {
    llvm::APInt llvmInt(INTEGER_BIT_SIZE, (uint64_t) literal.value, true /* signed */);
    return llvm::ConstantInt::get(*context, llvmInt);
}

llvm::Value* Generator::generate(const AST::StringLiteral& literal) {
    return builder.CreateGlobalStringPtr(literal.value, "globalstr");
}

// Generate a call to a function. Currently assumes it takes exactly one argument and the result is dropped because that
// is all a "Hello World!" program with putchar() requires.
llvm::CallInst* Generator::generate(const AST::FunctionCall& call) {
    llvm::Function* func = module->getFunction(call.callee);

    if (!func) throw UndeclaredException("Function \"" + call.callee + "\" not declared in this scope.");

    std::vector<llvm::Value*> arguments;
    for (const auto& arg : call.arguments) {
        arguments.push_back(arg->generate(*this));
    }

    return builder.CreateCall(func, arguments);
}

llvm::Value* Generator::generate(const AST::IdentifierExpr& identifier) {
    llvm::Value* value = namedValues[identifier.name];
    if (!value) throw UndeclaredException("Variable \"" + identifier.name + "\" not declared in this scope.");

    return value;
}