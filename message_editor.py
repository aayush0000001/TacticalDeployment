"""Message editor that uses a stack to support undoing additions."""

WIDE = "=" * 34
NARROW = "-" * 34


def banner(title):
    bar = "=" * len(title)
    print(f"\n{NARROW}\n\n{bar}\n{title}\n{bar}\n")


def current_message(stack):
    return "".join(stack)


def show_options():
    print(f"\n{WIDE}\n")
    print("Options\n=======\n")
    print("1. View\n2. Add\n3. Undo\n4. Quit\n")


def main():
    stack = []  # each entry is one addition; top of stack is the latest

    while True:
        show_options()
        choice = input("Enter choice (1 - 4): ").strip()

        if choice == "1":
            banner("View")
            print(current_message(stack))
        elif choice == "2":
            banner("Add to Message")
            message = current_message(stack)
            if message:
                print(message + "\n")
            stack.append(input("Enter addition: "))
        elif choice == "3":
            banner("Undo")
            if stack:
                stack.pop()
            else:
                print("Nothing to undo.\n")
            print(current_message(stack))
        elif choice == "4":
            banner("Final Version")
            print(current_message(stack))
            break
        else:
            print("\nInvalid choice. Please enter 1, 2, 3, or 4.")


if __name__ == "__main__":
    main()
