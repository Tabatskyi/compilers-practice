# Practice 8: Callable Objects and Code Cleanup
The main goal of this practice it to add some syntax sugar for function calls. Also you should revisit and refactor your code to ensure that the structure, UX and behavior follows the requirements

## Task 1. Callable object (1 point)
As we alreay have functions and member functions in the structure, we will add one minor feature that will allow us to use operator () on objects. To do that we will reserve `call` name for the function, so if we have declaration:
```
struct MyStruct
{
    i32 x
    i64 y

    fn call = (i32 t) -> bool
    {
        if t == x
        {
            return true
        }
        return false
    }
}
```
then we can use both function call syntax and callable object syntax
```
MyStruct object{10, 15}
bool mut res{false}
res = object.call(15)
bool mut otherRes{true}
otherRes = object(15)
```

From the perspective of changes that should be done in your compiler, it should mostly afect AST and semantics. So if you will find `ID "([Factor*] ")"` expression you current logic most likely verifies whether `ID` is a function name. But with callable object you will need to suppose that if it is not a function name, then it should be callable object so you should replace corresponding node with node that will cover member funciton call `ID "." "call" "([Factor*] ")"`. So if you will embed this logic to a proper place, then other parts will be as they were before.

## Task 2. Code Cleanup
During the previous 7 weeks yoo wrote a big amount of code and covered it with some tests. Some things may not fully cover the initial requirements or provide bad UX. So it is time to clean up the solution. Here are the main checkpoints:
1. The main program or produced binary must have name **compiler**, depending on the language it may be `compiler.py`, `compiler.cpp`. etc
2. The main program or script must have the following interface
```
compiler input.txt output.ll
```
so the first argument should provide a path to the source file, while the second is a path where generated IR should be stored.
**Note** you may have optional arguments for other functionality or debug purpose, but the command from above should work as described
**Note** in addition to the main program you may have helper scripts that will produce a target binary, but automatic checks will be executed on output.ll files produced by the main program on test input

3. Clean program output and make it more readable. There should be no extra output except report about successful stage completion(lexing, parsing, semantics), compilation success, or clear description of error type and line in the source file where the broken code is stored. Therefore you should extend lexer and parser to have **an additional field with line number** to use it on semantics stage. So the output can be as follows:
❌ Lexer error: unknown symbol @ at line N
❌ Syntax error: token X can't be used in this context
❌ Semantics error: variable x of type bool can't be assigned to variable y of type i32
✅ Syntax parsing done.
✅ Compilation completed successfully.
**Note** you can use emojis, colored text and so on to improve UX

4. There should not be any output from exception - just catch exception, handle it and convert to a compact and clear error report.
**Note** you can add optional flags that will allow to print more information, but default behavior should print only basic information.

5. Ensure that your project is well structured e.g. classes are split into separate files(one file can contain few classes if they are small or relevant, but those group shold cover certain aspects of functionality). Ensure that each item(function, class) follows the SRP.

