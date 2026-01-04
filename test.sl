type Integer = int;

let a: i8 = 65536;

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn sub(a: i32, b: i32) -> i32 { return a - b; }

let f: (i32, i32) -> i32 = add;
